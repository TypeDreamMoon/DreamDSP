#include "platform/PeakMeter.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

namespace dreamdsp {

PeakMeter::~PeakMeter()
{
    detach();
}

void PeakMeter::detach()
{
    if (m_meter) {
        m_meter->Release();
        m_meter = nullptr;
    }
    m_endpointId.clear();
}

bool PeakMeter::attach(const QString &endpointId)
{
    if (m_meter && m_endpointId == endpointId)
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

    const HRESULT hr = device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL,
                                        nullptr, reinterpret_cast<void **>(&m_meter));
    device->Release();

    if (FAILED(hr) || !m_meter) {
        m_meter = nullptr;
        return false;
    }

    m_endpointId = endpointId;
    return true;
}

float PeakMeter::peak() const
{
    if (!m_meter)
        return -1.0f;

    float value = 0.0f;
    if (FAILED(m_meter->GetPeakValue(&value)))
        return -1.0f;
    return value;
}

} // namespace dreamdsp
