// COM plumbing for the APO.
//
// Registration deliberately writes HKEY_CURRENT_USER\Software\Classes and
// nothing else. That is enough for CoCreateInstance to find the object, needs
// no elevation, and cannot affect audio: an APO only enters the audio graph
// once its CLSID is written into an endpoint's FxProperties, which is a
// separate, explicit step.

#include "DreamApo.h"

#include <windows.h>
#include <objbase.h>
#include <new>
#include <atomic>
#include <string>

using dreamdsp::apo::CLSID_DreamDspApo;
using dreamdsp::apo::DreamApo;

namespace {

std::atomic<long> g_lockCount{ 0 };
HMODULE g_module = nullptr;

class ClassFactory final : public IClassFactory
{
public:
    STDMETHOD_(ULONG, AddRef)() override { return m_ref.fetch_add(1) + 1; }
    STDMETHOD_(ULONG, Release)() override
    {
        const ULONG n = m_ref.fetch_sub(1) - 1;
        if (n == 0)
            delete this;
        return n;
    }
    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) {
            *ppv = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHOD(CreateInstance)(IUnknown *outer, REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;

        auto *apo = new (std::nothrow) DreamApo();
        if (!apo)
            return E_OUTOFMEMORY;

        const HRESULT hr = apo->QueryInterface(riid, ppv);
        apo->Release();
        return hr;
    }

    STDMETHOD(LockServer)(BOOL lock) override
    {
        lock ? g_lockCount.fetch_add(1) : g_lockCount.fetch_sub(1);
        return S_OK;
    }

private:
    std::atomic<ULONG> m_ref{ 1 };
};

std::wstring guidToString(REFGUID guid)
{
    wchar_t buf[64] = {};
    ::StringFromGUID2(guid, buf, 64);
    return buf;
}

LSTATUS writeString(HKEY root, const std::wstring &subKey, const wchar_t *name,
                    const std::wstring &value)
{
    HKEY key = nullptr;
    LSTATUS rc = ::RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0,
                                   KEY_WRITE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS)
        return rc;
    rc = ::RegSetValueExW(key, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE *>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);
    return rc;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        ::DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_DreamDspApo)
        return CLASS_E_CLASSNOTAVAILABLE;

    auto *factory = new (std::nothrow) ClassFactory();
    if (!factory)
        return E_OUTOFMEMORY;

    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return (g_lockCount.load() == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH] = {};
    if (::GetModuleFileNameW(g_module, path, MAX_PATH) == 0)
        return HRESULT_FROM_WIN32(::GetLastError());

    const std::wstring clsid = guidToString(CLSID_DreamDspApo);
    const std::wstring base = L"Software\\Classes\\CLSID\\" + clsid;

    // HKCU, not HKLM: no elevation, and trivially reversible.
    if (writeString(HKEY_CURRENT_USER, base, nullptr, L"DreamDSP Effects APO") != ERROR_SUCCESS)
        return E_ACCESSDENIED;
    if (writeString(HKEY_CURRENT_USER, base + L"\\InprocServer32", nullptr, path) != ERROR_SUCCESS)
        return E_ACCESSDENIED;
    if (writeString(HKEY_CURRENT_USER, base + L"\\InprocServer32", L"ThreadingModel",
                    L"Both") != ERROR_SUCCESS) {
        return E_ACCESSDENIED;
    }
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    const std::wstring clsid = guidToString(CLSID_DreamDspApo);
    const std::wstring base = L"Software\\Classes\\CLSID\\" + clsid;
    ::RegDeleteKeyW(HKEY_CURRENT_USER, (base + L"\\InprocServer32").c_str());
    ::RegDeleteKeyW(HKEY_CURRENT_USER, base.c_str());
    return S_OK;
}
