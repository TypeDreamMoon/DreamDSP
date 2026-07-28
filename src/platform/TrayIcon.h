#pragma once

#include <QAbstractNativeEventFilter>
#include <QImage>
#include <QObject>
#include <QPoint>

#include <windows.h>

namespace dreamdsp {

// A Windows notification-area icon.
//
// Deliberately not QSystemTrayIcon: that lives in QtWidgets and would force the
// application to be a QApplication, pulling the whole widget stack into a Qt
// Quick app just for one icon -- and its context menu is a native QMenu, which
// would not match the rest of the interface. Here the icon is native and the
// menu is left to QML.
//
// The callback message is delivered to an existing top-level window (the main
// window) rather than a dedicated message-only window, so that the
// TaskbarCreated broadcast -- which only reaches top-level windows -- is
// received and the icon can be re-added after an Explorer restart.
class TrayIcon : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit TrayIcon(QObject *parent = nullptr);
    ~TrayIcon() override;

    bool install(WId windowHandle);
    void uninstall();
    bool installed() const { return m_installed; }

    void setIcon(const QImage &image);
    void setToolTip(const QString &tip);

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void activated();                              // left click or double click
    void contextRequested(const QPoint &globalPos);

private:
    bool addOrModify(bool add);

    HWND m_hwnd = nullptr;
    HICON m_hIcon = nullptr;
    QString m_toolTip;
    bool m_installed = false;
    UINT m_taskbarCreated = 0;
};

// Converts an ARGB image to an HICON. The caller owns the result.
HICON createHIcon(const QImage &image);

} // namespace dreamdsp
