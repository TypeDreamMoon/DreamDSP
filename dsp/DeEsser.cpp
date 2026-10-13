#include "DeEsser.h"

#include <algorithm>

namespace dreamdsp::dsp {
namespace {
constexpr double kPi = 3.14159265358979323846;
float clean(float value, float fallback, float lo, float hi) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return (bits & 0x7F800000u) == 0x7F800000u ? fallback : clampTo(value, lo, hi);
}
bool validRate(double rate) noexcept
{
    uint64_t bits;
    std::memcpy(&bits, &rate, sizeof bits);
    return (bits & 0x7FF0000000000000ull) != 0x7FF0000000000000ull
           && rate >= 8000.0 && rate <= 192000.0;
}
} // namespace

void DeEsser::prepare(double sampleRate, int channels)
{
    m_supported = false; m_channels = 0; m_started = false;
    if (!validRate(sampleRate) || channels < 1 || channels > kMaxChannels) return;
    m_rate = sampleRate; m_channels = channels;
    for (auto *s : {&m_split, &m_threshold, &m_slope, &m_limit, &m_attack, &m_release}) s->prepare(m_rate);
    m_supported = true;
    setParams(m_p);
    reset();
}

void DeEsser::setParams(const Params &params) noexcept
{
    const Params defaults;
    m_p.frequencyHz = clean(params.frequencyHz, defaults.frequencyHz, 2000, 12000);
    m_p.thresholdDb = clean(params.thresholdDb, defaults.thresholdDb, -60, 0);
    m_p.ratio = clean(params.ratio, defaults.ratio, 1, 12);
    m_p.maxReductionDb = clean(params.maxReductionDb, defaults.maxReductionDb, 0, 18);
    m_p.attackMs = clean(params.attackMs, defaults.attackMs, 0.1f, 20);
    m_p.releaseMs = clean(params.releaseMs, defaults.releaseMs, 10, 500);
    const double frequency = std::min(double(m_p.frequencyHz), m_rate * 0.45);
    const double g = std::tan(kPi * frequency / m_rate);
    m_targetSplit = float(g / (1 + g));
    m_targetSlope = 1.0f - 1.0f / m_p.ratio;
    m_targetAttack = timeConstant(m_p.attackMs, m_rate);
    m_targetRelease = timeConstant(m_p.releaseMs, m_rate);
    m_split.setTarget(m_targetSplit); m_threshold.setTarget(m_p.thresholdDb);
    m_slope.setTarget(m_targetSlope); m_limit.setTarget(m_p.maxReductionDb);
    m_attack.setTarget(m_targetAttack); m_release.setTarget(m_targetRelease);
    if (!m_started) {
        m_split.reset(m_targetSplit); m_threshold.reset(m_p.thresholdDb);
        m_slope.reset(m_targetSlope); m_limit.reset(m_p.maxReductionDb);
        m_attack.reset(m_targetAttack); m_release.reset(m_targetRelease);
    }
}

void DeEsser::reset() noexcept
{
    m_lowState.fill(0);
    m_reduction = 0; m_started = false;
    m_split.reset(m_targetSplit); m_threshold.reset(m_p.thresholdDb);
    m_slope.reset(m_targetSlope); m_limit.reset(m_p.maxReductionDb);
    m_attack.reset(m_targetAttack); m_release.reset(m_targetRelease);
}

void DeEsser::process(const AudioBuffer &buffer) noexcept
{
    if (!m_supported || !buffer.valid()) return;
    m_started = true;
    const int channels = std::min(buffer.channelCount, m_channels);
    for (int i = 0; i < buffer.frames; ++i) {
        const float split = m_split.next(), threshold = m_threshold.next();
        const float slope = m_slope.next(), limit = m_limit.next();
        const float attack = m_attack.next(), release = m_release.next();
        float dry[kMaxChannels]{}, high[kMaxChannels]{};
        float detector = 0;
        for (int c = 0; c < channels; ++c) {
            if (!buffer.channels[c]) continue;
            bool bad = false;
            dry[c] = railed(buffer.channels[c][i], &bad);
            if (bad) m_lowState[size_t(c)] = 0;
            const float delta = split * (dry[c] - m_lowState[size_t(c)]);
            const float low = m_lowState[size_t(c)] + delta;
            m_lowState[size_t(c)] = low + delta;
            high[c] = dry[c] - low;
            detector = std::max(detector, std::fabs(high[c]));
        }
        const float overDb = 20.0f * std::log10(std::max(detector, 1e-12f)) - threshold;
        const float desired = clampTo(slope * overDb, 0.0f, limit);
        const float coefficient = desired > m_reduction ? attack : release;
        m_reduction = desired + coefficient * (m_reduction - desired);
        // A changing maximum also acts as a hard bound on the reported and
        // applied gain; the maximum itself moves through its parameter smoother.
        m_reduction = clampTo(m_reduction, 0.0f, limit);
        const float gain = dbToLin(-m_reduction);
        for (int c = 0; c < channels; ++c) {
            if (!buffer.channels[c]) continue;
            bool bad = false;
            buffer.channels[c][i] = m_reduction == 0.0f ? dry[c]
                : railed(dry[c] + (gain - 1.0f) * high[c], &bad);
            if (bad) m_lowState[size_t(c)] = 0;
        }
    }
}
} // namespace dreamdsp::dsp
