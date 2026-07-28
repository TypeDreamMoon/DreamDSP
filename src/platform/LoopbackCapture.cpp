#include "platform/LoopbackCapture.h"

#include "core/Fft.h"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <algorithm>
#include <cmath>

namespace dreamdsp {

namespace {

// 4096 points is 10.8 Hz per bin at 44.1 kHz. Smaller transforms cannot resolve
// the bottom two octaves: below roughly 200 Hz the log-spaced display bands are
// narrower than one bin, so several of them read the same value and the low end
// renders as a flat plateau. The cost is a 93 ms window, which is fine for a
// visual analyser.
constexpr int kFftSize = 4096;
constexpr int kHop = 1024;          // ~43 updates/s at 44.1 kHz
constexpr float kFloorDb = -90.0f;
constexpr double kFMin = 20.0;
constexpr double kFMax = 20000.0;

// Declared here rather than pulling in ksmedia.h for two GUIDs.
const GUID kSubtypeFloat = { 0x00000003, 0x0000, 0x0010,
                             { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
const GUID kSubtypePcm   = { 0x00000001, 0x0000, 0x0010,
                             { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

enum class SampleKind { Unsupported, Float32, Pcm16, Pcm32 };

SampleKind kindOf(const WAVEFORMATEX *fmt)
{
    if (!fmt)
        return SampleKind::Unsupported;

    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return SampleKind::Float32;
    if (fmt->wFormatTag == WAVE_FORMAT_PCM)
        return (fmt->wBitsPerSample == 16) ? SampleKind::Pcm16
             : (fmt->wBitsPerSample == 32) ? SampleKind::Pcm32
                                           : SampleKind::Unsupported;

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(fmt);
        if (::IsEqualGUID(ext->SubFormat, kSubtypeFloat))
            return SampleKind::Float32;
        if (::IsEqualGUID(ext->SubFormat, kSubtypePcm)) {
            return (fmt->wBitsPerSample == 16) ? SampleKind::Pcm16
                 : (fmt->wBitsPerSample == 32) ? SampleKind::Pcm32
                                               : SampleKind::Unsupported;
        }
    }
    return SampleKind::Unsupported;
}

// Average the channels down to mono; a spectrum per channel would be noise.
float frameToMono(const BYTE *data, int frameIndex, int channels, SampleKind kind)
{
    double sum = 0.0;
    for (int c = 0; c < channels; ++c) {
        const int i = frameIndex * channels + c;
        switch (kind) {
        case SampleKind::Float32:
            sum += reinterpret_cast<const float *>(data)[i];
            break;
        case SampleKind::Pcm16:
            sum += reinterpret_cast<const qint16 *>(data)[i] / 32768.0;
            break;
        case SampleKind::Pcm32:
            sum += reinterpret_cast<const qint32 *>(data)[i] / 2147483648.0;
            break;
        case SampleKind::Unsupported:
            return 0.0f;
        }
    }
    return float(sum / std::max(1, channels));
}

} // namespace

LoopbackCapture::LoopbackCapture(QObject *parent)
    : QThread(parent)
{
}

LoopbackCapture::~LoopbackCapture()
{
    stopCapture();
}

void LoopbackCapture::startCapture(const QString &endpointId)
{
    stopCapture();
    m_endpointId = endpointId;
    m_stop.storeRelaxed(0);
    start();
}

void LoopbackCapture::stopCapture()
{
    if (!isRunning())
        return;
    m_stop.storeRelaxed(1);
    if (!wait(1500))
        terminate();
}

void LoopbackCapture::run()
{
    const HRESULT coHr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool coOwned = SUCCEEDED(coHr);

    IMMDeviceEnumerator *enumerator = nullptr;
    IMMDevice *device = nullptr;
    IAudioClient *client = nullptr;
    IAudioCaptureClient *capture = nullptr;
    WAVEFORMATEX *mixFormat = nullptr;

    const auto cleanup = [&] {
        if (capture) capture->Release();
        if (client) { client->Stop(); client->Release(); }
        if (mixFormat) ::CoTaskMemFree(mixFormat);
        if (device) device->Release();
        if (enumerator) enumerator->Release();
        if (coOwned) ::CoUninitialize();
    };

    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void **>(&enumerator)))) {
        emit failed(QStringLiteral("无法创建设备枚举器"));
        cleanup();
        return;
    }

    if (m_endpointId.isEmpty()) {
        enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    } else {
        const std::wstring id = m_endpointId.toStdWString();
        enumerator->GetDevice(id.c_str(), &device);
    }
    if (!device) {
        emit failed(QStringLiteral("找不到该输出设备"));
        cleanup();
        return;
    }

    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void **>(&client)))
        || !client) {
        emit failed(QStringLiteral("无法打开音频客户端"));
        cleanup();
        return;
    }

    if (FAILED(client->GetMixFormat(&mixFormat)) || !mixFormat) {
        emit failed(QStringLiteral("无法取得混音格式"));
        cleanup();
        return;
    }

    const SampleKind kind = kindOf(mixFormat);
    if (kind == SampleKind::Unsupported) {
        emit failed(QStringLiteral("不支持的采样格式"));
        cleanup();
        return;
    }

    const int channels = mixFormat->nChannels;
    const double sampleRate = mixFormat->nSamplesPerSec;

    // 200 ms of buffer; loopback is shared-mode only, which is also why a
    // device in exclusive mode produces nothing here.
    constexpr REFERENCE_TIME kBufferDuration = 2'000'000;   // 100-ns units
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                  kBufferDuration, 0, mixFormat, nullptr))) {
        emit failed(QStringLiteral("无法开启 loopback(设备可能处于独占模式)"));
        cleanup();
        return;
    }

    if (FAILED(client->GetService(__uuidof(IAudioCaptureClient),
                                  reinterpret_cast<void **>(&capture)))
        || !capture) {
        emit failed(QStringLiteral("无法取得采集服务"));
        cleanup();
        return;
    }

    if (FAILED(client->Start())) {
        emit failed(QStringLiteral("无法启动采集"));
        cleanup();
        return;
    }

    // --- analysis setup ---------------------------------------------------
    const Fft fft(kFftSize);
    QVector<float> ring(kFftSize, 0.0f);
    int written = 0;             // samples added since the last transform
    int head = 0;                // next write position in the ring
    QVector<float> magnitudes;
    QVector<float> bands(kBands, kFloorDb);

    // Precompute which FFT bins belong to which display band.
    QVector<int> bandStart(kBands), bandEnd(kBands);
    const int binCount = kFftSize / 2 + 1;
    const double binHz = sampleRate / kFftSize;
    for (int b = 0; b < kBands; ++b) {
        const double f0 = kFMin * std::pow(kFMax / kFMin, double(b) / kBands);
        const double f1 = kFMin * std::pow(kFMax / kFMin, double(b + 1) / kBands);
        bandStart[b] = std::clamp(int(std::floor(f0 / binHz)), 1, binCount - 1);
        bandEnd[b] = std::clamp(int(std::ceil(f1 / binHz)), bandStart[b] + 1, binCount);
    }

    while (m_stop.loadRelaxed() == 0) {
        UINT32 packetSize = 0;
        if (FAILED(capture->GetNextPacketSize(&packetSize))) {
            break;
        }

        if (packetSize == 0) {
            // Nothing playing. Poll rather than using event callbacks, which
            // only work with loopback from Windows 10 1703 onwards.
            QThread::msleep(8);
            continue;
        }

        while (packetSize > 0 && m_stop.loadRelaxed() == 0) {
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;

            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            for (UINT32 i = 0; i < frames; ++i) {
                ring[head] = silent ? 0.0f : frameToMono(data, int(i), channels, kind);
                head = (head + 1) % kFftSize;
                ++written;

                if (written >= kHop) {
                    written = 0;

                    // Unwrap the ring into chronological order for the FFT.
                    QVector<float> frame(kFftSize);
                    for (int k = 0; k < kFftSize; ++k)
                        frame[k] = ring[(head + k) % kFftSize];

                    fft.magnitudeSpectrum(frame.constData(), &magnitudes);

                    for (int b = 0; b < kBands; ++b) {
                        float peak = 0.0f;
                        for (int bin = bandStart[b]; bin < bandEnd[b]; ++bin)
                            peak = std::max(peak, magnitudes[bin]);
                        // Peak per band rather than average: an averaged
                        // analyser looks flat and reads nothing like what you
                        // hear.
                        bands[b] = (peak <= 1e-7f)
                                       ? kFloorDb
                                       : std::max(kFloorDb, 20.0f * std::log10(peak));
                    }
                    emit spectrumReady(bands);
                }
            }

            capture->ReleaseBuffer(frames);
            if (FAILED(capture->GetNextPacketSize(&packetSize)))
                break;
        }
    }

    cleanup();
}

} // namespace dreamdsp
