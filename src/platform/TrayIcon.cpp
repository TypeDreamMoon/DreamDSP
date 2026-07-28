#include "platform/TrayIcon.h"

#include <QCoreApplication>
#include <shellapi.h>

namespace dreamdsp {

namespace {
// Our tray callback. WM_APP is the documented range for private messages sent
// to an application's own windows.
constexpr UINT kTrayCallback = WM_APP + 0x40;
constexpr UINT kTrayId = 1;
} // namespace

HICON createHIcon(const QImage &image)
{
    if (image.isNull())
        return nullptr;

    const QImage src = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = src.width();
    const int h = src.height();

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = w;
    bi.bV5Height = -h;          // negative: top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC screen = ::GetDC(nullptr);
    void *bits = nullptr;
    HBITMAP colour = ::CreateDIBSection(screen, reinterpret_cast<BITMAPINFO *>(&bi),
                                        DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, screen);
    if (!colour || !bits)
        return nullptr;

    for (int y = 0; y < h; ++y) {
        std::memcpy(static_cast<uchar *>(bits) + y * w * 4, src.constScanLine(y), w * 4);
    }

    // A monochrome mask is still required even for a 32-bit alpha icon.
    HBITMAP mask = ::CreateBitmap(w, h, 1, 1, nullptr);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = colour;
    ii.hbmMask = mask;
    HICON icon = ::CreateIconIndirect(&ii);

    ::DeleteObject(colour);
    ::DeleteObject(mask);
    return icon;
}

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
{
    m_taskbarCreated = ::RegisterWindowMessageW(L"TaskbarCreated");
}

TrayIcon::~TrayIcon()
{
    uninstall();
    if (m_hIcon)
        ::DestroyIcon(m_hIcon);
}

bool TrayIcon::install(WId windowHandle)
{
    if (m_installed)
        return true;
    m_hwnd = reinterpret_cast<HWND>(windowHandle);
    if (!m_hwnd)
        return false;

    QCoreApplication::instance()->installNativeEventFilter(this);
    m_installed = addOrModify(true);
    return m_installed;
}

void TrayIcon::uninstall()
{
    if (!m_installed)
        return;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayId;
    ::Shell_NotifyIconW(NIM_DELETE, &nid);

    if (auto *app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
    m_installed = false;
}

bool TrayIcon::addOrModify(bool add)
{
    if (!m_hwnd)
        return false;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayCallback;
    nid.hIcon = m_hIcon;

    const std::wstring tip = m_toolTip.toStdWString();
    ::wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);

    return ::Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &nid);
}

void TrayIcon::setIcon(const QImage &image)
{
    HICON fresh = createHIcon(image);
    if (!fresh)
        return;

    HICON old = m_hIcon;
    m_hIcon = fresh;
    if (m_installed)
        addOrModify(false);
    if (old)
        ::DestroyIcon(old);
}

void TrayIcon::setToolTip(const QString &tip)
{
    if (m_toolTip == tip)
        return;
    m_toolTip = tip;
    if (m_installed)
        addOrModify(false);
}

bool TrayIcon::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);
    if (eventType != QByteArrayLiteral("windows_generic_MSG"))
        return false;

    auto *msg = static_cast<MSG *>(message);
    if (msg->hwnd != m_hwnd)
        return false;

    // Explorer restarted and dropped every tray icon; put ours back.
    if (m_taskbarCreated != 0 && msg->message == m_taskbarCreated && m_installed) {
        addOrModify(true);
        return false;
    }

    if (msg->message != kTrayCallback)
        return false;

    switch (LOWORD(msg->lParam)) {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        emit activated();
        return true;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU: {
        POINT p{};
        ::GetCursorPos(&p);
        emit contextRequested(QPoint(p.x, p.y));
        return true;
    }
    default:
        break;
    }
    return false;
}

} // namespace dreamdsp
