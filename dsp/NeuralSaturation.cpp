#include "NeuralSaturation.h"
#include "models/SyntheticMemoryDrive.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dreamdsp::dsp {
namespace {
float safeParameter(float value, float fallback, float lo, float hi) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    if ((bits & 0x7f800000u) == 0x7f800000u) return fallback;
    return clampTo(value, lo, hi);
}
} // namespace

void NeuralSaturation::prepare(double sampleRate, int channels)
{
    // Running this discrete-time recurrent model at another rate changes its
    // learned memory. There is intentionally no pretend "2x" neural mode.
    m_supported = sampleRate == kModelSampleRate && channels >= 1 && channels <= kMaxChannels;
    m_channels = clampTo(channels, 0, kMaxChannels);
    m_rate = 1.0f - timeConstant(10.0f, kModelSampleRate);
    for (auto &channel : m_state) channel.dc.prepare(kModelSampleRate);
    setParams(m_p);
    reset();
}

void NeuralSaturation::setParams(const Params &p) noexcept
{
    m_p.driveDb = safeParameter(p.driveDb, 0.0f, -12.0f, 18.0f);
    m_p.mix = safeParameter(p.mix, 1.0f, 0.0f, 1.0f);
    m_p.outputDb = safeParameter(p.outputDb, 0.0f, -24.0f, 6.0f);
    m_p.model = 0; // This release ships exactly one immutable built-in model.
    m_targetDrive = dbToLin(m_p.driveDb);
    m_targetGain = dbToLin(m_p.outputDb);
    if (!m_hasProcessed) {
        m_drive = m_targetDrive;
        m_mix = m_p.mix;
        m_gain = m_targetGain;
    }
}

void NeuralSaturation::reset() noexcept
{
    for (auto &channel : m_state) {
        channel.hidden.fill(0.0f);
        channel.dc.reset();
    }
    m_drive = m_targetDrive;
    m_mix = m_p.mix;
    m_gain = m_targetGain;
    m_faultCount = 0;
    m_hasProcessed = false;
}

float NeuralSaturation::forward(float x, Channel &channel) noexcept
{
    using namespace synthetic_memory_drive;
    std::array<float, kUnits> next{};
    float result = kReadout[0] * x;
    // Read the complete previous state before committing any new unit: an
    // in-place update here would implement a different recurrent network.
    for (int j = 0; j < kUnits; ++j) {
        float activation = kInput[j] * x;
        for (int k = 0; k < kUnits; ++k)
            activation += kRecurrent[j][k] * channel.hidden[size_t(k)];
        const float old = channel.hidden[size_t(j)];
        const float h = old + kLeak[j] * (std::tanh(activation) - old);
        next[size_t(j)] = std::fabs(h) < 1e-20f ? 0.0f : h;
        result += kReadout[j + 1] * next[size_t(j)];
    }
    channel.hidden = next;
    return result;
}

void NeuralSaturation::fault(Channel &channel) noexcept
{
    channel.hidden.fill(0.0f);
    channel.dc.reset();
    if (m_faultCount != std::numeric_limits<uint32_t>::max()) ++m_faultCount;
}

void NeuralSaturation::process(const AudioBuffer &buf) noexcept
{
    if (!m_supported || !buf.valid()) return; // Unsupported rates pass bit-exactly.
    m_hasProcessed = true;
    const int channels = std::min(buf.channelCount, m_channels);
    for (int i = 0; i < buf.frames; ++i) {
        // Shared automation advances ONCE per stereo/multichannel sample.
        m_drive += m_rate * (m_targetDrive - m_drive);
        m_mix += m_rate * (m_p.mix - m_mix);
        m_gain += m_rate * (m_targetGain - m_gain);
        for (int c = 0; c < channels; ++c) {
            auto &channel = m_state[size_t(c)];
            bool bad = false;
            const float dry = railed(buf.channels[c][i], &bad);
            if (bad) {
                fault(channel);
                buf.channels[c][i] = 0.0f;
                continue;
            }
            const float x = clampTo(dry * m_drive, -2.0f, 2.0f);
            const float wet = channel.dc.process(forward(x, channel));
            const float value = m_gain * (dry + m_mix * (wet - dry));
            buf.channels[c][i] = railed(value, &bad);
            if (bad) {
                fault(channel);
                buf.channels[c][i] = dry;
            }
        }
    }
}

} // namespace dreamdsp::dsp
