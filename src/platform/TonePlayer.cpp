#include "platform/TonePlayer.h"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <cmath>
#include <vector>

namespace dreamdsp {

namespace {

template <typename T>
struct Com {
    T *p = nullptr;
    ~Com() { if (p) p->Release(); }
    T **operator&() { return &p; }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

const GUID kSubtypeFloat = { 0x00000003, 0x0000, 0x0010,
                             { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

} // namespace

QString playTone(const QString &endpointId, double seconds, double frequencyHz,
                 double amplitude)
{
    Com<IMMDeviceEnumerator> enumerator;
    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void **>(&enumerator)))
        || !enumerator) {
        return QStringLiteral("无法创建设备枚举器");
    }

    Com<IMMDevice> device;
    if (endpointId.isEmpty()) {
        enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    } else {
        const std::wstring id = endpointId.toStdWString();
        enumerator->GetDevice(id.c_str(), &device);
    }
    if (!device)
        return QStringLiteral("找不到端点");

    Com<IAudioClient> client;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void **>(&client)))
        || !client) {
        return QStringLiteral("无法激活音频客户端");
    }

    WAVEFORMATEX *mix = nullptr;
    if (FAILED(client->GetMixFormat(&mix)) || !mix)
        return QStringLiteral("无法取得混音格式");

    const int channels = mix->nChannels;
    const double rate = mix->nSamplesPerSec;
    const int bits = mix->wBitsPerSample;

    bool isFloat = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        isFloat = ::IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE *>(mix)->SubFormat,
                                kSubtypeFloat);
    }

    constexpr REFERENCE_TIME kBuffer = 2'000'000;   // 200 ms
    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBuffer, 0, mix, nullptr);
    if (FAILED(hr)) {
        ::CoTaskMemFree(mix);
        return QStringLiteral("Initialize 失败 (0x%1)").arg(quint32(hr), 8, 16, QLatin1Char('0'));
    }

    UINT32 bufferFrames = 0;
    client->GetBufferSize(&bufferFrames);

    Com<IAudioRenderClient> render;
    if (FAILED(client->GetService(__uuidof(IAudioRenderClient),
                                  reinterpret_cast<void **>(&render)))
        || !render) {
        ::CoTaskMemFree(mix);
        return QStringLiteral("无法取得渲染服务");
    }

    if (FAILED(client->Start())) {
        ::CoTaskMemFree(mix);
        return QStringLiteral("无法启动渲染");
    }

    const qint64 totalFrames = qint64(seconds * rate);
    qint64 written = 0;
    double phase = 0.0;
    const double step = 2.0 * 3.14159265358979323846 * frequencyHz / rate;

    while (written < totalFrames) {
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding)))
            break;

        const UINT32 available = bufferFrames - padding;
        if (available == 0) {
            ::Sleep(5);
            continue;
        }

        const UINT32 want = UINT32(qMin<qint64>(available, totalFrames - written));
        BYTE *data = nullptr;
        if (FAILED(render->GetBuffer(want, &data)) || !data)
            break;

        for (UINT32 i = 0; i < want; ++i) {
            const double v = amplitude * std::sin(phase);
            phase += step;
            for (int c = 0; c < channels; ++c) {
                if (isFloat && bits == 32) {
                    reinterpret_cast<float *>(data)[i * channels + c] = float(v);
                } else if (bits == 16) {
                    reinterpret_cast<qint16 *>(data)[i * channels + c] = qint16(v * 32767.0);
                } else if (bits == 32) {
                    reinterpret_cast<qint32 *>(data)[i * channels + c] = qint32(v * 2147483647.0);
                }
            }
        }

        render->ReleaseBuffer(want, 0);
        written += want;
        ::Sleep(5);
    }

    // Let the tail drain so the stream is genuinely live for its whole life.
    ::Sleep(200);
    client->Stop();
    ::CoTaskMemFree(mix);
    return {};
}

} // namespace dreamdsp
