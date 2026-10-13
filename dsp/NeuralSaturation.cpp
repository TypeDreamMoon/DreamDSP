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
    // Keep the recurrent dynamics at their training rate. Only this stage's
    // wet path is converted, with the dry path delayed to the same integer
    // host-sample latency. The native 48 kHz reference path remains unchanged.
    uint64_t bits;
    std::memcpy(&bits, &sampleRate, sizeof bits);
    m_supported = (bits & 0x7ff0000000000000ull) != 0x7ff0000000000000ull
        && sampleRate >= 8000 && sampleRate <= 192000 && channels >= 1 && channels <= kMaxChannels;
    m_channels = clampTo(channels, 0, kMaxChannels);
    m_converted = m_supported && sampleRate != kModelSampleRate;
    m_latency = 0;
    m_rate = 1.0f - timeConstant(10.0f, kModelSampleRate);
    m_hostRate = m_supported ? 1.0f - timeConstant(10.0f, sampleRate) : m_rate;
    if (m_converted) {
        m_hostPerModel = sampleRate / kModelSampleRate;
        m_modelPerHost = kModelSampleRate / sampleRate;
        m_toModel.prepare(sampleRate, kModelSampleRate);
        m_fromModel.prepare(kModelSampleRate, sampleRate);
        const double delay = m_toModel.delay() + m_fromModel.delay() * m_hostPerModel;
        m_latency = int(std::ceil(delay));
        m_extraModelDelay = (m_latency - m_toModel.delay()) * m_modelPerHost - m_fromModel.delay();
        m_input.assign(size_t(m_channels * 2 * StreamingSinc::kRing), 0);
        m_modelOutput.assign(m_input.size(), 0);
    }
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
    std::fill(m_input.begin(), m_input.end(), 0.0f);
    std::fill(m_modelOutput.begin(), m_modelOutput.end(), 0.0f);
    m_hostIndex = m_modelIndex = 0;
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
    for (int c = 0; c < channels; ++c) if (!buf.channels[c]) return;
    if (m_converted) { processConverted(buf, channels); return; }
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

void NeuralSaturation::processConverted(const AudioBuffer &buf, int channels) noexcept
{
    constexpr int ring = StreamingSinc::kRing;
    for (int i = 0; i < buf.frames; ++i, ++m_hostIndex) {
        m_mix += m_hostRate * (m_p.mix - m_mix);
        m_gain += m_hostRate * (m_targetGain - m_gain);
        for (int c = 0; c < channels; ++c) {
            bool bad = false;
            const float dry = railed(buf.channels[c][i], &bad);
            if (bad) {
                fault(m_state[size_t(c)]);
                std::fill_n(inputHistory(c), 2 * ring, 0.0f);
                std::fill_n(modelHistory(c), 2 * ring, 0.0f);
            }
            StreamingSinc::write(inputHistory(c), m_hostIndex, dry);
        }
        // At the lowest supported rate there are at most seven model ticks
        // per input frame; neither queue growth nor block-sized buffering.
        while (double(m_modelIndex) * m_hostPerModel <= double(m_hostIndex) + 1e-9) {
            m_drive += m_rate * (m_targetDrive - m_drive);
            for (int c = 0; c < channels; ++c) {
                auto &channel = m_state[size_t(c)];
                const float x = m_toModel.read(inputHistory(c), double(m_modelIndex) * m_hostPerModel);
                bool bad = false;
                float wet = railed(channel.dc.process(forward(clampTo(x * m_drive, -2.0f, 2.0f), channel)), &bad);
                if (bad) { fault(channel); wet = 0; }
                StreamingSinc::write(modelHistory(c), m_modelIndex, wet);
            }
            ++m_modelIndex;
        }
        const double wetTime = double(m_hostIndex) * m_modelPerHost - m_extraModelDelay;
        const auto dryAt = size_t(uint64_t(m_hostIndex - m_latency) & (ring - 1));
        for (int c = 0; c < channels; ++c) {
            const float dry = inputHistory(c)[dryAt];
            const float wet = m_fromModel.read(modelHistory(c), wetTime);
            bool bad = false;
            buf.channels[c][i] = railed(m_gain * (dry + m_mix * (wet - dry)), &bad);
            if (bad) { fault(m_state[size_t(c)]); buf.channels[c][i] = dry; }
        }
    }
}

} // namespace dreamdsp::dsp
