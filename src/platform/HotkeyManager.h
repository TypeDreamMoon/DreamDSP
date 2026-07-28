#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

class QSettings;

namespace dreamdsp {

// System-wide hotkeys via RegisterHotKey.
//
// Registered against the thread (hwnd == nullptr) rather than a window, so the
// bindings survive the main window being hidden into the tray.
//
// Caveat worth surfacing in the UI: while an elevated process has focus,
// Windows UIPI blocks these from reaching a non-elevated application. There is
// no way around that short of running elevated, which brings its own problems.
class HotkeyManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    struct ActionInfo {
        const char *id;
        const char *label;
    };
    static QVector<ActionInfo> actions();

    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    // mods is a Win32 MOD_* mask, vk a virtual-key code. vk == 0 clears.
    bool bind(const QString &actionId, quint32 mods, quint32 vk, QString *error = nullptr);
    void clear(const QString &actionId);
    void clearAll();

    bool hasBinding(const QString &actionId) const;
    QString displayText(const QString &actionId) const;

    void load(QSettings &settings);
    void save(QSettings &settings) const;

    // Qt keyboard modifiers + the event's native scan code -> Win32 mods/vk.
    // Scan code is used rather than Qt::Key so the physical key is captured
    // correctly regardless of layout.
    static bool translate(int qtModifiers, quint32 nativeScanCode,
                          quint32 *mods, quint32 *vk);
    static QString describe(quint32 mods, quint32 vk);

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void triggered(const QString &actionId);

private:
    struct Binding {
        quint32 mods = 0;
        quint32 vk = 0;
        int id = 0;
        bool registered = false;
    };

    bool doRegister(Binding &b, const QString &actionId, QString *error);
    void doUnregister(Binding &b);

    QHash<QString, Binding> m_bindings;
    int m_nextId = 0xB000;   // arbitrary base, well clear of other apps' ids
};

} // namespace dreamdsp
