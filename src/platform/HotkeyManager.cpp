#include "platform/HotkeyManager.h"

#include <QCoreApplication>
#include <QSettings>

#include <windows.h>

namespace dreamdsp {

QVector<HotkeyManager::ActionInfo> HotkeyManager::actions()
{
    return {
        { "toggleEq",     "开关均衡器" },
        { "toggleEngage", "接管 / 停止接管 config.txt" },
        { "gainUp",       "整体增益 +1 dB" },
        { "gainDown",     "整体增益 -1 dB" },
        { "preampUp",     "前置增益 +1 dB" },
        { "preampDown",   "前置增益 -1 dB" },
        { "resetAll",     "全部归零" },
        { "nextPreset",   "下一个预设" },
        { "prevPreset",   "上一个预设" },
        { "showWindow",   "显示 / 隐藏主窗口" },
    };
}

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager()
{
    clearAll();
    if (auto *app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
}

bool HotkeyManager::doRegister(Binding &b, const QString &actionId, QString *error)
{
    if (b.vk == 0)
        return true;

    if (b.id == 0)
        b.id = m_nextId++;

    // MOD_NOREPEAT stops a held key from firing continuously, which for
    // "gain +1 dB" would otherwise run away.
    if (!::RegisterHotKey(nullptr, b.id, b.mods | MOD_NOREPEAT, b.vk)) {
        const DWORD err = ::GetLastError();
        if (error) {
            *error = (err == ERROR_HOTKEY_ALREADY_REGISTERED)
                         ? QStringLiteral("%1 已被其它程序占用").arg(describe(b.mods, b.vk))
                         : QStringLiteral("注册 %1 失败 (错误 %2)").arg(describe(b.mods, b.vk)).arg(err);
        }
        b.registered = false;
        return false;
    }
    b.registered = true;
    Q_UNUSED(actionId);
    return true;
}

void HotkeyManager::doUnregister(Binding &b)
{
    if (b.registered && b.id != 0)
        ::UnregisterHotKey(nullptr, b.id);
    b.registered = false;
}

bool HotkeyManager::bind(const QString &actionId, quint32 mods, quint32 vk, QString *error)
{
    Binding &b = m_bindings[actionId];
    doUnregister(b);

    b.mods = mods;
    b.vk = vk;

    if (vk == 0)
        return true;

    if (!doRegister(b, actionId, error)) {
        b.mods = 0;
        b.vk = 0;
        return false;
    }
    return true;
}

void HotkeyManager::clear(const QString &actionId)
{
    auto it = m_bindings.find(actionId);
    if (it == m_bindings.end())
        return;
    doUnregister(it.value());
    it->mods = 0;
    it->vk = 0;
}

void HotkeyManager::clearAll()
{
    for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it)
        doUnregister(it.value());
}

bool HotkeyManager::hasBinding(const QString &actionId) const
{
    const auto it = m_bindings.constFind(actionId);
    return it != m_bindings.constEnd() && it->vk != 0;
}

QString HotkeyManager::displayText(const QString &actionId) const
{
    const auto it = m_bindings.constFind(actionId);
    if (it == m_bindings.constEnd() || it->vk == 0)
        return {};
    return describe(it->mods, it->vk);
}

bool HotkeyManager::translate(int qtModifiers, quint32 nativeScanCode,
                              quint32 *mods, quint32 *vk)
{
    if (!mods || !vk)
        return false;

    quint32 m = 0;
    if (qtModifiers & 0x02000000) m |= MOD_SHIFT;     // Qt::ShiftModifier
    if (qtModifiers & 0x04000000) m |= MOD_CONTROL;   // Qt::ControlModifier
    if (qtModifiers & 0x08000000) m |= MOD_ALT;       // Qt::AltModifier
    if (qtModifiers & 0x10000000) m |= MOD_WIN;       // Qt::MetaModifier

    // Scan code -> virtual key, layout-aware.
    const UINT v = ::MapVirtualKeyW(nativeScanCode, MAPVK_VSC_TO_VK_EX);
    if (v == 0)
        return false;

    // A bare modifier is not a hotkey.
    switch (v) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN:
        return false;
    default:
        break;
    }

    // Windows refuses an unmodified hotkey for ordinary keys, and grabbing a
    // bare letter system-wide would be hostile anyway. Media keys are fine.
    const bool mediaKey = (v >= VK_BROWSER_BACK && v <= VK_LAUNCH_APP2);
    if (m == 0 && !mediaKey && !(v >= VK_F1 && v <= VK_F24))
        return false;

    *mods = m;
    *vk = v;
    return true;
}

QString HotkeyManager::describe(quint32 mods, quint32 vk)
{
    if (vk == 0)
        return {};

    QStringList parts;
    if (mods & MOD_CONTROL) parts << QStringLiteral("Ctrl");
    if (mods & MOD_ALT)     parts << QStringLiteral("Alt");
    if (mods & MOD_SHIFT)   parts << QStringLiteral("Shift");
    if (mods & MOD_WIN)     parts << QStringLiteral("Win");

    // GetKeyNameText wants a scan code in bits 16-23, plus the extended flag.
    UINT scan = ::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    LONG lParam = static_cast<LONG>((scan & 0xFF) << 16);
    if ((scan >> 8) == 0xE0 || (scan >> 8) == 0xE1)
        lParam |= (1 << 24);

    wchar_t name[128] = {};
    QString keyName;
    if (::GetKeyNameTextW(lParam, name, static_cast<int>(std::size(name))) > 0)
        keyName = QString::fromWCharArray(name);
    if (keyName.isEmpty())
        keyName = QStringLiteral("VK_%1").arg(vk, 2, 16, QLatin1Char('0')).toUpper();

    parts << keyName;
    return parts.join(QStringLiteral(" + "));
}

void HotkeyManager::load(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("hotkeys"));
    for (const ActionInfo &a : actions()) {
        const QString id = QString::fromLatin1(a.id);
        const quint32 mods = settings.value(id + QStringLiteral("/mods"), 0).toUInt();
        const quint32 vk = settings.value(id + QStringLiteral("/vk"), 0).toUInt();
        if (vk != 0)
            bind(id, mods, vk);          // a clash with another app is not fatal
    }
    settings.endGroup();
}

void HotkeyManager::save(QSettings &settings) const
{
    settings.beginGroup(QStringLiteral("hotkeys"));
    for (const ActionInfo &a : actions()) {
        const QString id = QString::fromLatin1(a.id);
        const auto it = m_bindings.constFind(id);
        if (it == m_bindings.constEnd() || it->vk == 0) {
            settings.remove(id);
            continue;
        }
        settings.setValue(id + QStringLiteral("/mods"), it->mods);
        settings.setValue(id + QStringLiteral("/vk"), it->vk);
    }
    settings.endGroup();
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);
    if (eventType != QByteArrayLiteral("windows_generic_MSG"))
        return false;

    auto *msg = static_cast<MSG *>(message);
    if (msg->message != WM_HOTKEY)
        return false;

    const int id = static_cast<int>(msg->wParam);
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it) {
        if (it->registered && it->id == id) {
            emit triggered(it.key());
            return true;
        }
    }
    return false;
}

} // namespace dreamdsp
