#include "platform/AudioDevices.h"

// initguid.h must come first: it makes the CLSID_/IID_/PKEY_ constants below
// be *defined* here rather than merely declared, which saves us from having to
// link the (differently named across SDKs) GUID import libraries.
#include <initguid.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>

namespace dreamdsp {

namespace {

template <typename T>
struct ComPtr {
    T *p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T **operator&() { return &p; }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

QString friendlyName(IMMDevice *device)
{
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store)) || !store)
        return {};

    PROPVARIANT pv;
    ::PropVariantInit(&pv);
    QString name;
    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR)
        name = QString::fromWCharArray(pv.pwszVal);
    ::PropVariantClear(&pv);
    return name;
}

QString deviceId(IMMDevice *device)
{
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id)) || !id)
        return {};
    const QString out = QString::fromWCharArray(id);
    ::CoTaskMemFree(id);
    return out;
}

} // namespace

QVector<AudioDevice> enumerateRenderDevices(QString *error)
{
    QVector<AudioDevice> out;
    const auto fail = [error](const char *what) {
        if (error)
            *error = QString::fromLatin1(what);
    };

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        fail("CoCreateInstance(MMDeviceEnumerator) failed");
        return out;
    }

    QString defaultId;
    {
        ComPtr<IMMDevice> def;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &def)) && def)
            defaultId = deviceId(def.p);
    }

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(
        eRender, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED | DEVICE_STATE_DISABLED,
        &collection);
    if (FAILED(hr) || !collection) {
        fail("EnumAudioEndpoints failed");
        return out;
    }

    UINT count = 0;
    if (FAILED(collection->GetCount(&count)))
        return out;

    out.reserve(static_cast<int>(count));
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device)) || !device)
            continue;

        AudioDevice info;
        info.id = deviceId(device.p);
        info.name = friendlyName(device.p);
        if (info.name.isEmpty())
            info.name = QStringLiteral("(unnamed device)");
        info.isDefault = !info.id.isEmpty() && info.id == defaultId;

        DWORD state = 0;
        if (SUCCEEDED(device->GetState(&state)))
            info.active = (state == DEVICE_STATE_ACTIVE);

        out.push_back(info);
    }

    // Default first, then active devices, then the rest -- alphabetically within groups.
    std::sort(out.begin(), out.end(), [](const AudioDevice &a, const AudioDevice &b) {
        if (a.isDefault != b.isDefault) return a.isDefault;
        if (a.active != b.active)       return a.active;
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

int deviceSampleRate(const QString &endpointId)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void **>(&enumerator)))
        || !enumerator) {
        return 0;
    }

    ComPtr<IMMDevice> device;
    if (endpointId.isEmpty()) {
        enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    } else {
        const std::wstring id = endpointId.toStdWString();
        enumerator->GetDevice(id.c_str(), &device);
    }
    if (!device)
        return 0;

    ComPtr<IAudioClient> client;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void **>(&client)))
        || !client) {
        return 0;
    }

    WAVEFORMATEX *fmt = nullptr;
    if (FAILED(client->GetMixFormat(&fmt)) || !fmt)
        return 0;

    const int rate = int(fmt->nSamplesPerSec);
    ::CoTaskMemFree(fmt);
    return rate;
}

} // namespace dreamdsp
