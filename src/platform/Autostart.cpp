#include "platform/Autostart.h"

#include <QCoreApplication>
#include <QDir>

#include <windows.h>

namespace dreamdsp::autostart {

namespace {

constexpr const wchar_t *kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t *kValueName = L"DreamDSP";

QString readValue()
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return {};

    wchar_t buf[1024] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    const LSTATUS rc = ::RegQueryValueExW(key, kValueName, nullptr, &type,
                                          reinterpret_cast<LPBYTE>(buf), &size);
    ::RegCloseKey(key);

    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return {};
    return QString::fromWCharArray(buf);
}

} // namespace

QString command()
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return QStringLiteral("\"%1\" --tray").arg(exe);
}

bool isEnabled()
{
    const QString existing = readValue();
    if (existing.isEmpty())
        return false;

    // Only report enabled when the entry points at *this* build; a stale entry
    // from a previous location should read as off so toggling rewrites it.
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return existing.contains(exe, Qt::CaseInsensitive);
}

bool setEnabled(bool on, QString *error)
{
    HKEY key = nullptr;
    LSTATUS rc = ::RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_QUERY_VALUE,
                                   nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("无法打开注册表 Run 项 (%1)").arg(rc);
        return false;
    }

    if (on) {
        const std::wstring value = command().toStdWString();
        rc = ::RegSetValueExW(key, kValueName, 0, REG_SZ,
                              reinterpret_cast<const BYTE *>(value.c_str()),
                              static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = ::RegDeleteValueW(key, kValueName);
        if (rc == ERROR_FILE_NOT_FOUND)
            rc = ERROR_SUCCESS;   // already absent is success
    }
    ::RegCloseKey(key);

    if (rc != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("写入自启项失败 (%1)").arg(rc);
        return false;
    }
    return true;
}

} // namespace dreamdsp::autostart
