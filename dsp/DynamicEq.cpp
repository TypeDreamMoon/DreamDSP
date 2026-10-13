#include "DynamicEq.h"

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
bool validState(float value) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    // A Q=8 band can store more than the +/-4 output rail. This emergency
    // bound is above normal state levels and recovers from numerical faults.
    return (bits & 0x7F800000u) != 0x7F800000u && std::fabs(value) <= 128.0f;
}
} // namespace

void DynamicEq::prepare(double sampleRate, int channels)
{
    m_supported = false; m_channels = 0; m_started = false;
    if (!validRate(sampleRate) || channels < 1 || channels > kMaxChannels) return;
    m_rate = sampleRate; m_channels = channels;
    m_rampSamples = std::max(1, int(m_rate * 0.020));
    m_detectorRelease = timeConstant(10.0f, m_rate);
    for (auto &s : m_state)
        for (auto *p : {&s.g, &s.r, &s.threshold, &s.slope, &s.limit, &s.attack, &s.release})
            p->prepare(m_rate);
    m_supported = true;
    setParams(m_p);
    reset();
}

void DynamicEq::setParams(const Params &params) noexcept
{
    const Params defaults;
    for (int b = 0; b < kBands; ++b) {
        const auto &in = params.band[b];
        auto &p = m_p.band[b]; auto &s = m_state[size_t(b)];
        p.frequencyHz = clean(in.frequencyHz, defaults.band[b].frequencyHz, 40, 16000);
        p.q = clean(in.q, 1, 0.3f, 8);
        p.thresholdDb = clean(in.thresholdDb, -24, -60, 0);
        p.ratio = clean(in.ratio, 2, 1, 12);
        p.maxReductionDb = clean(in.maxReductionDb, 6, 0, 18);
        p.attackMs = clean(in.attackMs, 10, 0.1f, 100);
        p.releaseMs = clean(in.releaseMs, 120, 10, 1000);
        p.enabled = in.enabled == 1u ? 1u : 0u;
        // Trigonometry is performed only on parameter receipt, never per sample.
        s.targetG = float(std::tan(kPi * std::min(double(p.frequencyHz), 0.45 * m_rate) / m_rate));
        s.targetR = 1.0f / p.q;
        s.targetSlope = 1.0f - 1.0f / p.ratio;
        s.targetAttack = timeConstant(p.attackMs, m_rate);
        s.targetRelease = timeConstant(p.releaseMs, m_rate);
        s.g.setTarget(s.targetG); s.r.setTarget(s.targetR);
        s.threshold.setTarget(p.thresholdDb); s.slope.setTarget(s.targetSlope);
        s.limit.setTarget(p.maxReductionDb); s.attack.setTarget(s.targetAttack);
        s.release.setTarget(s.targetRelease);
        const float active = p.enabled && p.ratio > 1.0f && p.maxReductionDb > 0.0f ? 1.0f : 0.0f;
        if (active != s.blendTarget) {
            s.blendTarget = active; s.blendRemaining = m_rampSamples;
            s.blendStep = (s.blendTarget - s.blend) / float(m_rampSamples);
        }
        if (!m_started) {
            s.g.reset(s.targetG); s.r.reset(s.targetR); s.threshold.reset(p.thresholdDb);
            s.slope.reset(s.targetSlope); s.limit.reset(p.maxReductionDb);
            s.attack.reset(s.targetAttack); s.release.reset(s.targetRelease);
            s.blend = s.blendTarget; s.blendRemaining = 0;
        }
    }
}

void DynamicEq::reset() noexcept
{
    m_started = false;
    for (int b = 0; b < kBands; ++b) {
        auto &s = m_state[size_t(b)]; const auto &p = m_p.band[b];
        s.band.fill(0); s.low.fill(0); s.detector = s.reduction = s.appliedReduction = 0;
        s.g.reset(s.targetG); s.r.reset(s.targetR); s.threshold.reset(p.thresholdDb);
        s.slope.reset(s.targetSlope); s.limit.reset(p.maxReductionDb);
        s.attack.reset(s.targetAttack); s.release.reset(s.targetRelease);
        s.blend = s.blendTarget; s.blendRemaining = 0;
    }
}

float DynamicEq::reductionDb(int band) const noexcept
{
    return band >= 0 && band < kBands ? m_state[size_t(band)].appliedReduction : 0.0f;
}

void DynamicEq::process(const AudioBuffer &buffer) noexcept
{
    if (!m_supported || !buffer.valid()) return;
    m_started = true;
    const int channels = std::min(buffer.channelCount, m_channels);
    for (int i = 0; i < buffer.frames; ++i) {
        float dry[kMaxChannels]{};
        for (int c = 0; c < channels; ++c) {
            if (!buffer.channels[c]) continue;
            bool bad = false;
            dry[c] = railed(buffer.channels[c][i], &bad);
            if (bad) for (auto &s : m_state) s.band[size_t(c)] = s.low[size_t(c)] = 0;
        }
        for (auto &s : m_state) {
            const float g = s.g.next(), r = s.r.next(), threshold = s.threshold.next();
            const float slope = s.slope.next(), limit = s.limit.next();
            const float attack = s.attack.next(), release = s.release.next();
            if (s.blendRemaining > 0) {
                s.blend += s.blendStep;
                if (--s.blendRemaining == 0) {
                    s.blend = s.blendTarget;
                    if (s.blend == 0.0f) {
                        s.band.fill(0); s.low.fill(0); s.detector = s.reduction = s.appliedReduction = 0;
                    }
                }
            }
            if (s.blend == 0.0f) continue;
            const float denominator = 1.0f / (1.0f + g * (g + r));
            float selected[kMaxChannels]{};
            float peak = 0;
            for (int c = 0; c < channels; ++c) {
                if (!buffer.channels[c]) continue;
                const size_t channel = size_t(c);
                const float bp = (s.band[channel] + g * (dry[c] - s.low[channel])) * denominator;
                const float lp = s.low[channel] + g * bp;
                s.band[channel] = 2.0f * bp - s.band[channel];
                s.low[channel] = 2.0f * lp - s.low[channel];
                if (!validState(s.band[channel]) || !validState(s.low[channel])) {
                    s.band[channel] = s.low[channel] = 0;
                    continue;
                }
                selected[c] = r * bp;
                peak = std::max(peak, std::fabs(selected[c]));
            }
            s.detector = std::max(peak, s.detector * m_detectorRelease);
            const float overDb = 20.0f * std::log10(std::max(s.detector, 1e-12f)) - threshold;
            const float desired = clampTo(slope * overDb, 0.0f, limit);
            const float coefficient = desired > s.reduction ? attack : release;
            s.reduction = clampTo(desired + coefficient * (s.reduction - desired), 0.0f, limit);
            const float cut = s.blend * (dbToLin(-s.reduction) - 1.0f);
            s.appliedReduction = cut == 0.0f ? 0.0f : -20.0f * std::log10(1.0f + cut);
            if (cut == 0.0f) continue;
            for (int c = 0; c < channels; ++c) {
                if (!buffer.channels[c]) continue;
                bool bad = false;
                dry[c] = railed(dry[c] + cut * selected[c], &bad);
                if (bad) s.band[size_t(c)] = s.low[size_t(c)] = 0;
            }
        }
        for (int c = 0; c < channels; ++c)
            if (buffer.channels[c]) buffer.channels[c][i] = dry[c];
    }
}
} // namespace dreamdsp::dsp
