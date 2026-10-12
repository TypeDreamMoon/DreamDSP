#include "platform/ApoInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include <vector>

#include <windows.h>
#include <shellapi.h>
#include <winsvc.h>

namespace dreamdsp {

namespace {

const wchar_t *kClsid = L"{6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E10}";
const wchar_t *kEngineKey =
    L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\{6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E10}";
const wchar_t *kClsidKey =
    L"SOFTWARE\\Classes\\CLSID\\{6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E10}";

// The post-mix (GFX) slot. Pre-mix is left alone so that an existing Equalizer
// APO installation keeps working ahead of us rather than being displaced twice.
const wchar_t *kSlotValue = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2";

// PKEY_CompositeFX_EndpointEffectClsid: the endpoint-effect (EFX) stage of a
// Windows 11 composite chain, a REG_MULTI_SZ list of processing objects run in
// order after the mix. Its neighbours ,13 and ,14 are the stream and mode
// stages.
//
// The reason this exists: once any of the composite lists is present on an
// endpoint, the audio engine builds the chain from them and does not read the
// legacy slots at all. A driver that ships its own effects this way -- Realtek's
// USB audio package does, from RtkMicUUn64.dll -- therefore switches DreamDSP
// off on that endpoint without touching a single one of our values. The legacy
// slot still names us, the CLSID is still registered, the DLL still loads when
// asked; audiodg simply never asks. Found that way: the log stops on the day
// the driver's composite keys appear and nothing else changes.
//
// EFX rather than the stream or mode stage because it is the composite
// equivalent of where we always sat -- once per endpoint, after every stream
// has been mixed -- and appended rather than prepended so that the limiter's
// ceiling is the last thing before the converter, as it is everywhere else.
const wchar_t *kCompositeEfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},15";
const wchar_t *kCompositeSfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},13";
const wchar_t *kCompositeMfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},14";

// PKEY_AudioEndpoint_Disable_SysFx. One DWORD, and while it is 1 Windows builds
// no effect chain for the endpoint at all -- no processing object is
// instantiated, nothing is logged, and every registration still reads as
// correct. It is the box the Sound control panel calls "Disable all
// enhancements", and Equalizer APO's uninstaller sets it when it hands an
// endpoint back.
//
// Which is how a working install becomes a silent one: DreamDSP still in the
// post-mix slot, still registered, still loadable, and audiodg never going to
// ask for it.
const wchar_t *kDisableSysFx = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";

QString programDataDir() { return QStringLiteral("C:/ProgramData/DreamDSP"); }
QString stagedDllPath()  { return programDataDir() + QStringLiteral("/DreamDspApo.dll"); }

QString sourceDllPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/DreamDspApo.dll");
}

// "{0.0.0.00000000}.{30d0a993-...}" -> "{30d0a993-...}". A bare guid passes
// through unchanged, so callers may hand us either form.
QString endpointGuid(const QString &endpointId)
{
    const int at = endpointId.indexOf(QStringLiteral("}."));
    return (at >= 0) ? endpointId.mid(at + 2) : endpointId;
}

QString fxPropertiesKey(const QString &endpointId)
{
    return QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio"
                          "\\Render\\%1\\FxProperties").arg(endpointGuid(endpointId));
}

bool keyExists(HKEY root, const wchar_t *sub)
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    ::RegCloseKey(key);
    return true;
}

QString readString(HKEY root, const QString &sub, const QString &name)
{
    HKEY key = nullptr;
    // KEY_QUERY_VALUE only. Asking for more on the MMDevices keys is refused
    // outright even for Administrators -- see docs/apo-registration.md.
    if (::RegOpenKeyExW(root, reinterpret_cast<const wchar_t *>(sub.utf16()),
                        0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return {};
    }

    wchar_t buffer[512] = {};
    DWORD bytes = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS rc = ::RegQueryValueExW(key, reinterpret_cast<const wchar_t *>(name.utf16()),
                                          nullptr, &type,
                                          reinterpret_cast<BYTE *>(buffer), &bytes);
    ::RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return {};
    return QString::fromWCharArray(buffer);
}

// Writing a value into a key we may not open for full access. RegOpenKeyEx with
// exactly KEY_SET_VALUE | KEY_QUERY_VALUE succeeds where anything broader --
// including everything the convenience wrappers do -- is denied.
LSTATUS writeStringPrecise(HKEY root, const QString &sub, const QString &name,
                           const QString &value)
{
    HKEY key = nullptr;
    const LSTATUS open = ::RegOpenKeyExW(root, reinterpret_cast<const wchar_t *>(sub.utf16()),
                                         0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key);
    if (open != ERROR_SUCCESS)
        return open;

    const LSTATUS rc = ::RegSetValueExW(
        key, reinterpret_cast<const wchar_t *>(name.utf16()), 0, REG_SZ,
        reinterpret_cast<const BYTE *>(value.utf16()),
        DWORD((value.size() + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);
    return rc;
}

// The same precise-rights dance as the string pair above. Returns `dflt` when
// the value is absent, which for the sysfx switch means "not disabled".
DWORD readDwordPrecise(HKEY root, const QString &sub, const QString &name, DWORD dflt)
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, reinterpret_cast<const wchar_t *>(sub.utf16()),
                        0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return dflt;
    }
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    DWORD type = 0;
    const LSTATUS rc = ::RegQueryValueExW(key, reinterpret_cast<const wchar_t *>(name.utf16()),
                                          nullptr, &type,
                                          reinterpret_cast<BYTE *>(&value), &bytes);
    ::RegCloseKey(key);
    return (rc == ERROR_SUCCESS && type == REG_DWORD) ? value : dflt;
}

LSTATUS writeDwordPrecise(HKEY root, const QString &sub, const QString &name, DWORD value)
{
    HKEY key = nullptr;
    const LSTATUS open = ::RegOpenKeyExW(root, reinterpret_cast<const wchar_t *>(sub.utf16()),
                                         0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key);
    if (open != ERROR_SUCCESS)
        return open;
    const LSTATUS rc = ::RegSetValueExW(key, reinterpret_cast<const wchar_t *>(name.utf16()),
                                        0, REG_DWORD,
                                        reinterpret_cast<const BYTE *>(&value), sizeof(value));
    ::RegCloseKey(key);
    return rc;
}

// A REG_MULTI_SZ, as a list. `present` reports whether the value exists at
// all, which for the composite keys is the whole question: an empty list still
// switches the endpoint to composite mode.
QStringList readMultiPrecise(HKEY root, const QString &sub, const QString &name,
                             bool *present = nullptr)
{
    if (present)
        *present = false;
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, reinterpret_cast<const wchar_t *>(sub.utf16()),
                        0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return {};
    }
    DWORD type = 0, bytes = 0;
    const wchar_t *wname = reinterpret_cast<const wchar_t *>(name.utf16());
    LSTATUS rc = ::RegQueryValueExW(key, wname, nullptr, &type, nullptr, &bytes);
    if (rc != ERROR_SUCCESS || (type != REG_MULTI_SZ && type != REG_SZ)) {
        ::RegCloseKey(key);
        return {};
    }
    std::vector<wchar_t> buf(bytes / sizeof(wchar_t) + 2, L'\0');
    rc = ::RegQueryValueExW(key, wname, nullptr, &type,
                            reinterpret_cast<BYTE *>(buf.data()), &bytes);
    ::RegCloseKey(key);
    if (rc != ERROR_SUCCESS)
        return {};
    if (present)
        *present = true;

    QStringList out;
    for (const wchar_t *p = buf.data(); *p; p += wcslen(p) + 1)
        out << QString::fromWCharArray(p);
    return out;
}

LSTATUS writeMultiPrecise(HKEY root, const QString &sub, const QString &name,
                          const QStringList &values)
{
    HKEY key = nullptr;
    const LSTATUS open = ::RegOpenKeyExW(root, reinterpret_cast<const wchar_t *>(sub.utf16()),
                                         0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key);
    if (open != ERROR_SUCCESS)
        return open;

    std::vector<wchar_t> blob;
    for (const QString &v : values) {
        const std::wstring w = v.toStdWString();
        blob.insert(blob.end(), w.begin(), w.end());
        blob.push_back(L'\0');
    }
    blob.push_back(L'\0');
    const LSTATUS rc = ::RegSetValueExW(key, reinterpret_cast<const wchar_t *>(name.utf16()),
                                        0, REG_MULTI_SZ,
                                        reinterpret_cast<const BYTE *>(blob.data()),
                                        DWORD(blob.size() * sizeof(wchar_t)));
    ::RegCloseKey(key);
    return rc;
}

bool containsClsid(const QStringList &list, const QString &clsid)
{
    for (const QString &v : list)
        if (v.compare(clsid, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

LSTATUS deleteValuePrecise(HKEY root, const QString &sub, const QString &name)
{
    HKEY key = nullptr;
    const LSTATUS open = ::RegOpenKeyExW(root, reinterpret_cast<const wchar_t *>(sub.utf16()),
                                         0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key);
    if (open != ERROR_SUCCESS)
        return open;
    const LSTATUS rc = ::RegDeleteValueW(key, reinterpret_cast<const wchar_t *>(name.utf16()));
    ::RegCloseKey(key);
    return rc;
}

// Where the CLSID we displaced when attaching is kept, so detaching can put it
// back. HKCU: this is our own bookkeeping and needs no elevation.
QString backupKey(const QString &endpointId)
{
    return QStringLiteral("Software\\DreamDSP\\ApoBackup\\%1").arg(endpointGuid(endpointId));
}

LSTATUS writeHkcuString(const QString &sub, const QString &name, const QString &value)
{
    HKEY key = nullptr;
    LSTATUS rc = ::RegCreateKeyExW(HKEY_CURRENT_USER,
                                   reinterpret_cast<const wchar_t *>(sub.utf16()),
                                   0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS)
        return rc;
    rc = ::RegSetValueExW(key, reinterpret_cast<const wchar_t *>(name.utf16()), 0, REG_SZ,
                          reinterpret_cast<const BYTE *>(value.utf16()),
                          DWORD((value.size() + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);
    return rc;
}

QString lastErrorText(LSTATUS rc)
{
    wchar_t *text = nullptr;
    const DWORD n = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, DWORD(rc), 0, reinterpret_cast<wchar_t *>(&text), 0, nullptr);
    QString out = (n > 0 && text) ? QString::fromWCharArray(text, int(n)).trimmed()
                                  : QStringLiteral("错误 %1").arg(rc);
    if (text)
        ::LocalFree(text);
    return out;
}

} // namespace

ApoState apoState()
{
    ApoState s;
    s.sourceDll = sourceDllPath();
    s.stagedDll = stagedDllPath();

    const QFileInfo staged(s.stagedDll);
    const QFileInfo source(s.sourceDll);
    s.dllStaged = staged.exists();

    // "Up to date" deliberately compares size and timestamp rather than
    // hashing: the point is to notice a rebuild during development, not to
    // defend against anything.
    s.upToDate = s.dllStaged && source.exists()
                 && staged.size() == source.size()
                 && staged.lastModified() >= source.lastModified();

    s.clsidRegistered = keyExists(HKEY_LOCAL_MACHINE, kClsidKey);
    s.engineRegistered = keyExists(HKEY_LOCAL_MACHINE, kEngineKey);
    return s;
}

namespace {

ChainEntry describeClsid(const QString &clsid)
{
    ChainEntry e;
    e.clsid = clsid;
    e.isOurs = clsid.compare(QString::fromWCharArray(kClsid), Qt::CaseInsensitive) == 0;
    e.name = readString(HKEY_LOCAL_MACHINE,
                        QStringLiteral("SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\%1")
                            .arg(clsid),
                        QStringLiteral("FriendlyName"));
    e.dll = readString(HKEY_LOCAL_MACHINE,
                       QStringLiteral("SOFTWARE\\Classes\\CLSID\\%1\\InprocServer32").arg(clsid),
                       QString());
    if (e.isOurs && e.name.isEmpty())
        e.name = QStringLiteral("DreamDSP Effects");
    return e;
}

ChainStage readStage(const QString &fx, const wchar_t *value, const char *key,
                     const QString &label, bool *present)
{
    ChainStage st;
    st.key = QString::fromLatin1(key);
    st.label = label;
    bool here = false;
    const QStringList list = readMultiPrecise(HKEY_LOCAL_MACHINE, fx,
                                              QString::fromWCharArray(value), &here);
    for (const QString &c : list) {
        // An all-zero GUID is how drivers spell "nothing here".
        if (c.isEmpty() || c.startsWith(QLatin1String("{00000000-0000-0000-0000-000000000000")))
            continue;
        st.entries << describeClsid(c);
    }
    if (present)
        *present = here;
    return st;
}

} // namespace

EndpointChain endpointChain(const QString &endpointId)
{
    EndpointChain ch;
    if (endpointId.isEmpty())
        return ch;
    const QString fx = fxPropertiesKey(endpointId);

    ch.sysFxDisabled = readDwordPrecise(HKEY_LOCAL_MACHINE, fx,
                                        QString::fromWCharArray(kDisableSysFx), 0) != 0;

    bool hasS = false, hasM = false, hasE = false;
    ChainStage cs = readStage(fx, kCompositeSfx, "SFX", QStringLiteral("每路音频流各自经过,混音之前"), &hasS);
    ChainStage cm = readStage(fx, kCompositeMfx, "MFX", QStringLiteral("按处理模式分组,混音之前"), &hasM);
    ChainStage ce = readStage(fx, kCompositeEfx, "EFX", QStringLiteral("整个设备一次,混音之后"), &hasE);
    ch.composite = hasS || hasM || hasE;

    // The legacy slots. Pre-mix and post-mix are the Vista-era pair; ,5 ,6 ,7
    // are the Windows 8.1 single-CLSID versions of the three composite stages.
    ChainStage lpre  = readStage(fx, L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1", "LFX",
                                 QStringLiteral("旧式 · 混音之前"), nullptr);
    ChainStage lpost = readStage(fx, kSlotValue, "GFX",
                                 QStringLiteral("旧式 · 混音之后"), nullptr);
    ChainStage ls    = readStage(fx, L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5", "SFX",
                                 QStringLiteral("旧式单槽 · 每路音频流"), nullptr);
    ChainStage lm    = readStage(fx, L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6", "MFX",
                                 QStringLiteral("旧式单槽 · 处理模式"), nullptr);
    ChainStage le    = readStage(fx, L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7", "EFX",
                                 QStringLiteral("旧式单槽 · 整个设备"), nullptr);

    QVector<ChainStage> legacy;
    for (ChainStage *st : { &lpre, &lpost, &ls, &lm, &le })
        if (!st->entries.isEmpty())
            legacy << *st;

    if (ch.composite) {
        ch.stages << cs << cm << ce;
        for (ChainStage st : legacy) {
            st.ignored = true;
            ch.stages << st;
        }
    } else {
        ch.stages = legacy;
    }

    for (const ChainStage &st : ch.stages) {
        for (const ChainEntry &e : st.entries) {
            if (!e.isOurs)
                continue;
            if (st.ignored)
                ch.oursInert = true;
            else
                ch.oursPresent = true;
        }
    }
    if (ch.oursPresent)
        ch.oursInert = false;
    return ch;
}

ApoSlot apoSlotOf(const QString &endpointId)
{
    ApoSlot slot;
    if (endpointId.isEmpty())
        return slot;

    // Read before the early return on an empty slot: a device with its effects
    // switched off is worth reporting whether or not anything holds the slot,
    // because that is precisely the state where holding it means nothing.
    slot.sysFxDisabled = readDwordPrecise(HKEY_LOCAL_MACHINE, fxPropertiesKey(endpointId),
                                          QString::fromWCharArray(kDisableSysFx), 0) != 0;

    // Composite first: if the endpoint has a composite chain, it is the only
    // thing the engine reads, whatever the legacy slot says.
    const QString fx = fxPropertiesKey(endpointId);
    bool hasSfx = false, hasMfx = false, hasEfx = false;
    readMultiPrecise(HKEY_LOCAL_MACHINE, fx, QString::fromWCharArray(kCompositeSfx), &hasSfx);
    readMultiPrecise(HKEY_LOCAL_MACHINE, fx, QString::fromWCharArray(kCompositeMfx), &hasMfx);
    const QStringList efx = readMultiPrecise(HKEY_LOCAL_MACHINE, fx,
                                             QString::fromWCharArray(kCompositeEfx), &hasEfx);
    slot.composite = hasSfx || hasMfx || hasEfx;
    if (slot.composite) {
        slot.isOurs = containsClsid(efx, QString::fromWCharArray(kClsid));
        if (slot.isOurs) {
            slot.clsid = QString::fromWCharArray(kClsid);
            slot.friendlyName = QStringLiteral("DreamDSP Effects");
        }
        return slot;
    }

    slot.clsid = readString(HKEY_LOCAL_MACHINE, fx, QString::fromWCharArray(kSlotValue));
    if (slot.clsid.isEmpty())
        return slot;

    slot.isOurs = (slot.clsid.compare(QString::fromWCharArray(kClsid), Qt::CaseInsensitive) == 0);
    slot.friendlyName = readString(
        HKEY_LOCAL_MACHINE,
        QStringLiteral("SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\%1").arg(slot.clsid),
        QStringLiteral("FriendlyName"));
    if (slot.isOurs && slot.friendlyName.isEmpty())
        slot.friendlyName = QStringLiteral("DreamDSP Effects");
    return slot;
}

QString attachApo(const QString &endpointId)
{
    if (endpointId.isEmpty())
        return QStringLiteral("没有选中设备");

    const ApoSlot current = apoSlotOf(endpointId);

    // Clearing the endpoint's sysfx switch is part of attaching rather than a
    // separate courtesy: with it set Windows builds no chain, so a slot written
    // underneath it is a registration that will never be asked for.
    //
    // Done even when the slot is already ours, because that is exactly what
    // something else leaves behind on its way out -- and the resulting state
    // reads as fully working from every angle except the sound.
    if (current.sysFxDisabled) {
        writeHkcuString(backupKey(endpointId), QStringLiteral("SysFxWasDisabled"),
                        QStringLiteral("1"));
        const LSTATUS fx = writeDwordPrecise(HKEY_LOCAL_MACHINE, fxPropertiesKey(endpointId),
                                             QString::fromWCharArray(kDisableSysFx), 0);
        if (fx != ERROR_SUCCESS) {
            return QStringLiteral("无法开启这台设备的系统音效:%1").arg(lastErrorText(fx));
        }
    }

    if (current.isOurs)
        return {};   // the slot was already ours; nothing to displace or back up

    if (current.composite) {
        // Joined, not taken over: the driver's own endpoint effects stay in the
        // list and keep running, and detach removes exactly the one entry we
        // added. Nothing is displaced, so nothing needs backing up beyond the
        // fact that we were here.
        const QString fx = fxPropertiesKey(endpointId);
        QStringList efx = readMultiPrecise(HKEY_LOCAL_MACHINE, fx,
                                           QString::fromWCharArray(kCompositeEfx));
        efx << QString::fromWCharArray(kClsid);
        const LSTATUS rc = writeMultiPrecise(HKEY_LOCAL_MACHINE, fx,
                                             QString::fromWCharArray(kCompositeEfx), efx);
        if (rc != ERROR_SUCCESS)
            return QStringLiteral("无法写入设备的复合效果链:%1").arg(lastErrorText(rc));
        writeHkcuString(backupKey(endpointId), QStringLiteral("CompositeEfx"),
                        QStringLiteral("1"));
        return {};
    }

    // Remember what we are about to displace, before displacing it. An empty
    // string records "the slot was free", which detach must honour by removing
    // the value rather than writing an empty one.
    writeHkcuString(backupKey(endpointId), QStringLiteral("PostMix"), current.clsid);

    const LSTATUS rc = writeStringPrecise(HKEY_LOCAL_MACHINE, fxPropertiesKey(endpointId),
                                          QString::fromWCharArray(kSlotValue),
                                          QString::fromWCharArray(kClsid));
    if (rc != ERROR_SUCCESS)
        return QStringLiteral("无法写入设备的效果槽位:%1").arg(lastErrorText(rc));
    return {};
}

QString detachApo(const QString &endpointId)
{
    if (endpointId.isEmpty())
        return QStringLiteral("没有选中设备");

    const ApoSlot current = apoSlotOf(endpointId);

    if (current.composite) {
        // Remove our entry and only ours. The legacy slot may still name us
        // from before the driver switched the endpoint over; that value is
        // inert while the composite keys exist, but it is ours, so it is put
        // back below as well.
        const QString fx = fxPropertiesKey(endpointId);
        QStringList efx = readMultiPrecise(HKEY_LOCAL_MACHINE, fx,
                                           QString::fromWCharArray(kCompositeEfx));
        QStringList kept;
        for (const QString &v : efx)
            if (v.compare(QString::fromWCharArray(kClsid), Qt::CaseInsensitive) != 0)
                kept << v;
        if (kept.size() != efx.size()) {
            const LSTATUS rc = writeMultiPrecise(HKEY_LOCAL_MACHINE, fx,
                                                 QString::fromWCharArray(kCompositeEfx), kept);
            if (rc != ERROR_SUCCESS)
                return QStringLiteral("无法还原设备的复合效果链:%1").arg(lastErrorText(rc));
        }
        const QString legacy = readString(HKEY_LOCAL_MACHINE, fx,
                                          QString::fromWCharArray(kSlotValue));
        if (legacy.compare(QString::fromWCharArray(kClsid), Qt::CaseInsensitive) != 0) {
            ::RegDeleteKeyW(HKEY_CURRENT_USER,
                            reinterpret_cast<const wchar_t *>(backupKey(endpointId).utf16()));
            return {};
        }
        // Fall through to restore the legacy slot from the backup.
    } else if (!current.isOurs && !current.clsid.isEmpty()) {
        // Someone else owns the slot now. Leave it alone rather than deleting
        // another product's registration.
        return {};
    }

    HKEY key = nullptr;
    QString previous;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER,
                        reinterpret_cast<const wchar_t *>(backupKey(endpointId).utf16()),
                        0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        wchar_t buffer[512] = {};
        DWORD bytes = sizeof(buffer);
        DWORD type = 0;
        if (::RegQueryValueExW(key, L"PostMix", nullptr, &type,
                               reinterpret_cast<BYTE *>(buffer), &bytes) == ERROR_SUCCESS
            && type == REG_SZ) {
            previous = QString::fromWCharArray(buffer);
        }
        ::RegCloseKey(key);
    }

    const LSTATUS rc = previous.isEmpty()
        ? deleteValuePrecise(HKEY_LOCAL_MACHINE, fxPropertiesKey(endpointId),
                             QString::fromWCharArray(kSlotValue))
        : writeStringPrecise(HKEY_LOCAL_MACHINE, fxPropertiesKey(endpointId),
                             QString::fromWCharArray(kSlotValue), previous);

    // A value that was already absent is not a failure.
    if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND)
        return QStringLiteral("无法还原设备的效果槽位:%1").arg(lastErrorText(rc));

    ::RegDeleteKeyW(HKEY_CURRENT_USER,
                    reinterpret_cast<const wchar_t *>(backupKey(endpointId).utf16()));
    return {};
}

QString runElevated(const QStringList &args)
{
    const QString exe = QCoreApplication::applicationFilePath();
    const QString params = args.join(QLatin1Char(' '));

    const std::wstring exeW = QDir::toNativeSeparators(exe).toStdWString();
    const std::wstring paramsW = params.toStdWString();

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    // NOCLOSEPROCESS so we can wait: the caller needs to know the machine-wide
    // state has actually changed before it re-reads it.
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = exeW.c_str();
    info.lpParameters = paramsW.c_str();
    info.nShow = SW_HIDE;

    if (!::ShellExecuteExW(&info)) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_CANCELLED)
            return QStringLiteral("已取消");
        return QStringLiteral("无法提权:%1").arg(lastErrorText(LSTATUS(err)));
    }
    if (!info.hProcess)
        return QStringLiteral("提权后未能启动");

    ::WaitForSingleObject(info.hProcess, 120000);
    DWORD exitCode = 1;
    ::GetExitCodeProcess(info.hProcess, &exitCode);
    ::CloseHandle(info.hProcess);

    if (exitCode != 0)
        return QStringLiteral("提权操作失败(退出码 %1)").arg(exitCode);
    return {};
}

// --------------------------------------------------------- elevated section

namespace {

LSTATUS writeHklmString(const QString &sub, const wchar_t *name, const QString &value)
{
    HKEY key = nullptr;
    LSTATUS rc = ::RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                                   reinterpret_cast<const wchar_t *>(sub.utf16()),
                                   0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS)
        return rc;
    rc = ::RegSetValueExW(key, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE *>(value.utf16()),
                          DWORD((value.size() + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);
    return rc;
}

LSTATUS writeHklmDword(const QString &sub, const wchar_t *name, DWORD value)
{
    HKEY key = nullptr;
    LSTATUS rc = ::RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                                   reinterpret_cast<const wchar_t *>(sub.utf16()),
                                   0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS)
        return rc;
    rc = ::RegSetValueExW(key, name, 0, REG_DWORD,
                          reinterpret_cast<const BYTE *>(&value), sizeof(value));
    ::RegCloseKey(key);
    return rc;
}

} // namespace

QString performApoInstall()
{
    const QString source = sourceDllPath();
    if (!QFile::exists(source))
        return QStringLiteral("找不到 %1").arg(source);

    QDir().mkpath(programDataDir());
    // The parameter file lives here. Created now so the directory exists with
    // the right access before anything tries to write into it.
    QDir().mkpath(programDataDir() + QStringLiteral("/control"));

    const QString staged = stagedDllPath();

    // Stopping Audiosrv does not make audiodg.exe exit instantly, and while it
    // is alive the staged DLL stays mapped and cannot be replaced. Waiting is
    // the whole difference between an install that works and one that silently
    // leaves the old build in place -- which is exactly what happened the first
    // time this ran.
    if (QFile::exists(staged)) {
        bool removed = false;
        for (int attempt = 0; attempt < 40 && !removed; ++attempt) {   // ~8 s
            removed = QFile::remove(staged);
            if (!removed)
                ::Sleep(200);
        }
        if (!removed) {
            return QStringLiteral("旧的 DreamDspApo.dll 仍被音频引擎占用 —— "
                                  "audiodg.exe 没有随音频服务退出");
        }
    }
    if (!QFile::copy(source, staged))
        return QStringLiteral("无法复制到 %1").arg(staged);

    // audiodg.exe runs as LOCAL SERVICE and must be able to read the DLL, and
    // to append to the diagnostic log beside it.
    {
        QProcess icacls;
        icacls.start(QStringLiteral("icacls.exe"),
                     { QDir::toNativeSeparators(programDataDir()),
                       QStringLiteral("/grant"), QStringLiteral("*S-1-5-19:(OI)(CI)M"),
                       QStringLiteral("/T") });
        icacls.waitForFinished(30000);
    }

    const QString clsidKey = QString::fromWCharArray(kClsidKey);
    LSTATUS rc = writeHklmString(clsidKey, nullptr, QStringLiteral("DreamDSP Effects APO"));
    if (rc == ERROR_SUCCESS)
        rc = writeHklmString(clsidKey + QStringLiteral("\\InprocServer32"), nullptr,
                             QDir::toNativeSeparators(staged));
    if (rc == ERROR_SUCCESS)
        rc = writeHklmString(clsidKey + QStringLiteral("\\InprocServer32"),
                             L"ThreadingModel", QStringLiteral("Both"));
    if (rc != ERROR_SUCCESS)
        return QStringLiteral("注册 COM 组件失败:%1").arg(lastErrorText(rc));

    // The entry that actually decides whether the engine will look at us at
    // all. Values mirror APO_REG_PROPERTIES in apo/DreamApo.cpp and must stay
    // in step with it.
    const QString engineKey = QString::fromWCharArray(kEngineKey);
    rc = writeHklmString(engineKey, L"FriendlyName", QStringLiteral("DreamDSP Effects"));
    if (rc == ERROR_SUCCESS) rc = writeHklmString(engineKey, L"Copyright", QStringLiteral("DreamDSP"));
    if (rc == ERROR_SUCCESS) rc = writeHklmDword(engineKey, L"MajorVersion", 0);
    if (rc == ERROR_SUCCESS) rc = writeHklmDword(engineKey, L"MinorVersion", 1);
    // 13 = APO_FLAG_INPLACE | FRAMESPERSECOND_MUST_MATCH | BITSPERSAMPLE_MUST_MATCH
    if (rc == ERROR_SUCCESS) rc = writeHklmDword(engineKey, L"Flags", 13);
    if (rc == ERROR_SUCCESS) rc = writeHklmDword(engineKey, L"MinInputConnections", 1);
    if (rc == ERROR_SUCCESS) rc = writeHklmDword(engineKey, L"MaxInputConnections", 1);
    if (rc == ERROR_SUCCESS) rc = writeHklmDword(engineKey, L"MinOutputConnections", 1);
    if (rc == ERROR_SUCCESS) rc = writeHklmDword(engineKey, L"MaxOutputConnections", 1);
    // Unlimited. Zero would mean no instance may ever be created.
    if (rc == ERROR_SUCCESS) rc = writeHklmDword(engineKey, L"MaxInstances", 0xFFFFFFFFu);
    if (rc == ERROR_SUCCESS) rc = writeHklmDword(engineKey, L"NumAPOInterfaces", 1);
    if (rc == ERROR_SUCCESS)
        rc = writeHklmString(engineKey, L"APOInterface0",
                             QStringLiteral("{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}"));
    if (rc != ERROR_SUCCESS)
        return QStringLiteral("注册音频引擎属性失败:%1").arg(lastErrorText(rc));

    return {};
}

QString performApoUninstall()
{
    ::RegDeleteTreeW(HKEY_LOCAL_MACHINE, kEngineKey);
    ::RegDeleteTreeW(HKEY_LOCAL_MACHINE, kClsidKey);
    // The staged DLL is left in place: audiodg usually still has it mapped, and
    // an unregistered DLL sitting in ProgramData is harmless.
    return {};
}

namespace {

bool waitForState(SC_HANDLE service, DWORD wanted, int timeoutMs)
{
    SERVICE_STATUS status = {};
    const int step = 200;
    for (int waited = 0; waited < timeoutMs; waited += step) {
        if (!::QueryServiceStatus(service, &status))
            return false;
        if (status.dwCurrentState == wanted)
            return true;
        ::Sleep(step);
    }
    return false;
}

bool stopService(SC_HANDLE manager, const QString &name, QStringList *stopped);

// Stopping a service means stopping whatever depends on it first, or the SCM
// simply refuses. Audiosrv has several dependents on a typical machine
// (Realtek's service, ASUS lighting, the per-user AarSvc instances).
bool stopDependents(SC_HANDLE manager, SC_HANDLE service, QStringList *stopped)
{
    DWORD bytes = 0;
    DWORD count = 0;
    ::EnumDependentServicesW(service, SERVICE_ACTIVE, nullptr, 0, &bytes, &count);
    if (bytes == 0)
        return true;

    QByteArray buffer(int(bytes), Qt::Uninitialized);
    auto *entries = reinterpret_cast<LPENUM_SERVICE_STATUSW>(buffer.data());
    if (!::EnumDependentServicesW(service, SERVICE_ACTIVE, entries, bytes, &bytes, &count))
        return false;

    for (DWORD i = 0; i < count; ++i) {
        if (!stopService(manager, QString::fromWCharArray(entries[i].lpServiceName), stopped))
            return false;
    }
    return true;
}

bool stopService(SC_HANDLE manager, const QString &name, QStringList *stopped)
{
    SC_HANDLE service = ::OpenServiceW(manager, reinterpret_cast<const wchar_t *>(name.utf16()),
                                       SERVICE_STOP | SERVICE_QUERY_STATUS
                                           | SERVICE_ENUMERATE_DEPENDENTS);
    if (!service)
        return false;

    bool ok = stopDependents(manager, service, stopped);
    if (ok) {
        SERVICE_STATUS status = {};
        if (::QueryServiceStatus(service, &status) && status.dwCurrentState != SERVICE_STOPPED) {
            ::ControlService(service, SERVICE_CONTROL_STOP, &status);
            ok = waitForState(service, SERVICE_STOPPED, 30000);
            if (ok && stopped)
                stopped->prepend(name);   // restart in reverse order
        }
    }
    ::CloseServiceHandle(service);
    return ok;
}

bool startService(SC_HANDLE manager, const QString &name)
{
    SC_HANDLE service = ::OpenServiceW(manager, reinterpret_cast<const wchar_t *>(name.utf16()),
                                       SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service)
        return false;
    ::StartServiceW(service, 0, nullptr);
    const bool ok = waitForState(service, SERVICE_RUNNING, 30000);
    ::CloseServiceHandle(service);
    return ok;
}

} // namespace

QString stopAudioService(QStringList *stopped)
{
    SC_HANDLE manager = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return QStringLiteral("无法连接服务控制管理器(需要管理员权限)");

    const bool ok = stopService(manager, QStringLiteral("Audiosrv"), stopped);
    ::CloseServiceHandle(manager);
    return ok ? QString() : QStringLiteral("无法停止音频服务");
}

QString startAudioService(const QStringList &stopped)
{
    SC_HANDLE manager = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return QStringLiteral("无法连接服务控制管理器(需要管理员权限)");

    // Audiosrv first, then whatever depended on it, in the reverse of the
    // order they were taken down.
    startService(manager, QStringLiteral("Audiosrv"));
    for (const QString &name : stopped) {
        if (name.compare(QStringLiteral("Audiosrv"), Qt::CaseInsensitive) != 0)
            startService(manager, name);
    }
    ::CloseServiceHandle(manager);
    return {};
}

QString restartAudioService()
{
    QStringList stopped;
    const QString err = stopAudioService(&stopped);

    // Bring everything back up regardless of how the stop went: leaving the
    // machine without audio would be far worse than a partial restart.
    startAudioService(stopped);
    return err;
}

} // namespace dreamdsp
