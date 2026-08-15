#include "platform/VolumeReader.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

namespace dreamdsp {

VolumeReader::~VolumeReader()
{
    detach();
}

void VolumeReader::detach()
{
    if (m_volume) {
        m_volume->Release();
        m_volume = nullptr;
    }
    m_endpointId.clear();
    m_minDb = -60.0f;
}

bool VolumeReader::attach(const QString &endpointId)
{
    if (m_volume && m_endpointId == endpointId)
        return true;
    detach();

    IMMDeviceEnumerator *enumerator = nullptr;
    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void **>(&enumerator)))
        || !enumerator) {
        return false;
    }

    IMMDevice *device = nullptr;
    if (endpointId.isEmpty()) {
        enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    } else {
        const std::wstring id = endpointId.toStdWString();
        enumerator->GetDevice(id.c_str(), &device);
    }
    enumerator->Release();

    if (!device)
        return false;

    const HRESULT hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                                        nullptr, reinterpret_cast<void **>(&m_volume));
    device->Release();

    if (FAILED(hr) || !m_volume) {
        m_volume = nullptr;
        return false;
    }

    float minDb = -60.0f, maxDb = 0.0f, step = 0.0f;
    if (SUCCEEDED(m_volume->GetVolumeRange(&minDb, &maxDb, &step)))
        m_minDb = minDb;

    m_endpointId = endpointId;
    return true;
}

float VolumeReader::levelDb() const
{
    if (!m_volume)
        return 1.0f;

    float db = 0.0f;
    if (FAILED(m_volume->GetMasterVolumeLevel(&db)))
        return 1.0f;

    // Muted counts as the bottom of the range rather than as full volume: the
    // correction should not be computed against a level nothing is playing at.
    BOOL muted = FALSE;
    if (SUCCEEDED(m_volume->GetMute(&muted)) && muted)
        return m_minDb;

    return db > 0.0f ? 0.0f : db;
}

} // namespace dreamdsp
