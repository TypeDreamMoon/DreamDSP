#include "NoiseGate.h"

#include <algorithm>

namespace dreamdsp::dsp {
namespace {
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

void NoiseGate::prepare(double sampleRate, int channels)
{
    m_supported = false; m_channels = 0; m_started = false;
    if (!validRate(sampleRate) || channels < 1 || channels > kMaxChannels) return;
    m_rate = sampleRate; m_channels = channels;
    m_rampSamples = std::max(1, int(m_rate * 0.020));
    m_detectorRelease = timeConstant(10.0f, m_rate);
    for (auto *s : {&m_threshold, &m_hysteresis, &m_floor, &m_attack, &m_hold, &m_release})
        s->prepare(m_rate);
    m_supported = true;
    setParams(m_p);
    reset();
}

void NoiseGate::setParams(const Params &params) noexcept
{
    const Params defaults;
    m_p.thresholdDb = clean(params.thresholdDb, defaults.thresholdDb, -80, 0);
    m_p.hysteresisDb = clean(params.hysteresisDb, defaults.hysteresisDb, 0, 18);
    m_p.rangeDb = clean(params.rangeDb, defaults.rangeDb, 0, 80);
    m_p.attackMs = clean(params.attackMs, defaults.attackMs, 0.1f, 50);
    m_p.holdMs = clean(params.holdMs, defaults.holdMs, 0, 500);
    m_p.releaseMs = clean(params.releaseMs, defaults.releaseMs, 10, 1000);
    m_targetThreshold = dbToLin(m_p.thresholdDb);
    m_targetHysteresis = dbToLin(-m_p.hysteresisDb);
    m_targetFloor = dbToLin(-m_p.rangeDb);
    m_targetAttack = timeConstant(m_p.attackMs, m_rate);
    m_targetHold = float(m_rate * 0.001 * m_p.holdMs);
    m_targetRelease = timeConstant(m_p.releaseMs, m_rate);
    m_threshold.setTarget(m_targetThreshold); m_hysteresis.setTarget(m_targetHysteresis);
    m_floor.setTarget(m_targetFloor); m_attack.setTarget(m_targetAttack);
    m_hold.setTarget(m_targetHold); m_release.setTarget(m_targetRelease);
    const float bypass = m_p.rangeDb == 0.0f ? 1.0f : 0.0f;
    if (bypass != m_bypassTarget) {
        m_bypassTarget = bypass;
        m_bypassRemaining = m_rampSamples;
        m_bypassStep = (m_bypassTarget - m_bypass) / float(m_rampSamples);
    }
    if (!m_started) {
        m_threshold.reset(m_targetThreshold); m_hysteresis.reset(m_targetHysteresis);
        m_floor.reset(m_targetFloor); m_attack.reset(m_targetAttack);
        m_hold.reset(m_targetHold); m_release.reset(m_targetRelease);
        m_bypass = m_bypassTarget; m_bypassRemaining = 0;
        m_gain = m_targetFloor; m_reduction = m_p.rangeDb;
    }
}

void NoiseGate::reset() noexcept
{
    m_detector = 0; m_quietSamples = 0; m_open = false; m_started = false;
    m_threshold.reset(m_targetThreshold); m_hysteresis.reset(m_targetHysteresis);
    m_floor.reset(m_targetFloor); m_attack.reset(m_targetAttack);
    m_hold.reset(m_targetHold); m_release.reset(m_targetRelease);
    m_bypass = m_bypassTarget; m_bypassRemaining = 0;
    m_gain = m_targetFloor; m_reduction = m_p.rangeDb;
}

void NoiseGate::process(const AudioBuffer &buffer) noexcept
{
    if (!m_supported || !buffer.valid()) return;
    m_started = true;
    const int channels = std::min(buffer.channelCount, m_channels);
    for (int i = 0; i < buffer.frames; ++i) {
        const float threshold = m_threshold.next(), hysteresis = m_hysteresis.next();
        const float floor = m_floor.next(), attack = m_attack.next();
        const float hold = m_hold.next(), release = m_release.next();
        float dry[kMaxChannels]{};
        float peak = 0;
        for (int c = 0; c < channels; ++c) {
            if (!buffer.channels[c]) continue;
            bool bad = false;
            dry[c] = railed(buffer.channels[c][i], &bad);
            peak = std::max(peak, std::fabs(dry[c]));
        }
        m_detector = std::max(peak, m_detector * m_detectorRelease);
        if (m_detector >= threshold) {
            m_open = true; m_quietSamples = 0;
        } else if (m_open && m_detector < threshold * hysteresis) {
            // Saturation keeps the counter bounded even during arbitrarily
            // long silence. ceil makes a noninteger hold last at least holdMs.
            m_quietSamples = std::min(m_quietSamples + 1, 96001);
            if (float(m_quietSamples) >= hold) m_open = false;
        } else {
            m_quietSamples = 0;
        }
        const float target = m_open ? 1.0f : floor;
        const float coefficient = target > m_gain ? attack : release;
        m_gain = clampTo(target + coefficient * (m_gain - target), floor, 1.0f);
        if (m_bypassRemaining > 0) {
            m_bypass += m_bypassStep;
            if (--m_bypassRemaining == 0) m_bypass = m_bypassTarget;
        }
        const float gain = m_bypass == 1.0f ? 1.0f : m_gain + m_bypass * (1.0f - m_gain);
        for (int c = 0; c < channels; ++c) {
            if (!buffer.channels[c]) continue;
            buffer.channels[c][i] = gain == 1.0f ? dry[c] : dry[c] * gain;
        }
        m_reduction = gain == 1.0f ? 0.0f : -20.0f * std::log10(std::max(gain, 1e-4f));
    }
}
} // namespace dreamdsp::dsp
