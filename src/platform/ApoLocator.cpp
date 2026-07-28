#include "platform/ApoLocator.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>

#include <windows.h>

#pragma comment(lib, "version.lib")

namespace dreamdsp {

namespace {

QString regString(HKEY root, const wchar_t *subKey, const wchar_t *value)
{
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof(buf);
    const LSTATUS rc = ::RegGetValueW(root, subKey, value,
                                      RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                                      nullptr, buf, &size);
    if (rc != ERROR_SUCCESS)
        return {};
    return QString::fromWCharArray(buf);
}

QString fileVersion(const QString &path)
{
    const std::wstring wpath = path.toStdWString();
    DWORD dummy = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(wpath.c_str(), &dummy);
    if (size == 0)
        return {};

    QByteArray block(static_cast<int>(size), Qt::Uninitialized);
    if (!::GetFileVersionInfoW(wpath.c_str(), 0, size, block.data()))
        return {};

    VS_FIXEDFILEINFO *info = nullptr;
    UINT len = 0;
    if (!::VerQueryValueW(block.constData(), L"\\", reinterpret_cast<LPVOID *>(&info), &len)
        || info == nullptr) {
        return {};
    }

    return QStringLiteral("%1.%2.%3.%4")
        .arg(HIWORD(info->dwFileVersionMS))
        .arg(LOWORD(info->dwFileVersionMS))
        .arg(HIWORD(info->dwFileVersionLS))
        .arg(LOWORD(info->dwFileVersionLS));
}

// APO's installer grants Authenticated Users modify rights on the config
// directory, so we normally do not need elevation -- but a non-standard
// install path or an AV product can break that assumption. Probe for real.
bool probeWritable(const QString &dir)
{
    if (dir.isEmpty() || !QFileInfo::exists(dir))
        return false;

    QTemporaryFile probe(QDir(dir).filePath(QStringLiteral("dreamdsp-XXXXXX.tmp")));
    probe.setAutoRemove(true);
    return probe.open();
}

} // namespace

ApoInstall locateApo()
{
    ApoInstall apo;

    const wchar_t *kKey = L"SOFTWARE\\EqualizerAPO";
    apo.installPath = QDir::fromNativeSeparators(regString(HKEY_LOCAL_MACHINE, kKey, L"InstallPath"));
    apo.configPath = QDir::fromNativeSeparators(regString(HKEY_LOCAL_MACHINE, kKey, L"ConfigPath"));

    if (apo.installPath.isEmpty() && apo.configPath.isEmpty())
        return apo;

    // Tolerate a missing ConfigPath by falling back, but only as a last resort.
    if (apo.configPath.isEmpty() && !apo.installPath.isEmpty())
        apo.configPath = apo.installPath + QStringLiteral("/config");

    apo.found = QFileInfo::exists(apo.configPath);
    apo.version = fileVersion(QDir(apo.installPath).filePath(QStringLiteral("EqualizerAPO.dll")));
    apo.configWritable = apo.found && probeWritable(apo.configPath);
    return apo;
}

QString configFilePath(const ApoInstall &apo, const QString &fileName)
{
    if (apo.configPath.isEmpty())
        return {};
    return QDir(apo.configPath).filePath(fileName);
}

} // namespace dreamdsp
