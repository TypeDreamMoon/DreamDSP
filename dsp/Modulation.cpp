#include "Modulation.h"
#include <algorithm>

namespace dreamdsp::dsp {
namespace {
float safeParameter(float value, float lo, float hi, float fallback) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    if ((bits & 0x7F800000u) == 0x7F800000u) return fallback;
    return clampTo(value, lo, hi);
}
double safeRate(double rate) noexcept
{
    uint64_t bits;
    std::memcpy(&bits, &rate, sizeof bits);
    if ((bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull
        || rate < 8000.0 || rate > 384000.0) return 48000.0;
    return rate;
}
} // namespace
void Modulation::prepare(double rate, int channels)
{
    m_rate = safeRate(rate);
    m_channels = clampTo(channels, 1, kChannels);
    m_length = int(m_rate * 0.1) + 4;
    for (int c = 0; c < m_channels; ++c) m_delay[c].assign(size_t(m_length), 0.0f);
    for (auto *s : {&m_speed, &m_depth, &m_time, &m_regen, &m_mix, &m_stereo}) s->prepare(m_rate);
    reset();
}
void Modulation::setParams(const Params &p) noexcept
{
    const Params fallback;
    m_p.rateHz = safeParameter(p.rateHz, 0.02f, 10.0f, fallback.rateHz);
    m_p.depth = safeParameter(p.depth, 0.0f, 1.0f, fallback.depth);
    m_p.delayMs = safeParameter(p.delayMs, 0.1f, 30.0f, fallback.delayMs);
    m_p.feedback = safeParameter(p.feedback, -0.85f, 0.85f, fallback.feedback);
    m_p.mix = safeParameter(p.mix, 0.0f, 1.0f, 0.0f);
    m_p.stereoPhase = safeParameter(p.stereoPhase, 0.0f, 0.5f, fallback.stereoPhase);
    m_speed.setTarget(m_p.rateHz);
    m_depth.setTarget(m_p.depth);
    m_time.setTarget(m_p.delayMs);
    m_regen.setTarget(m_p.feedback);
    m_mix.setTarget(m_p.mix);
    m_stereo.setTarget(m_p.stereoPhase);
}
void Modulation::reset() noexcept
{
    m_phase = 0.0;
    m_write = 0;
    for (auto &d : m_delay) std::fill(d.begin(), d.end(), 0.0f);
    std::memset(m_ap, 0, sizeof m_ap);
    std::memset(m_feedback, 0, sizeof m_feedback);
    m_speed.reset(clampTo(m_p.rateHz, 0.02f, 10.0f));
    m_depth.reset(clampTo(m_p.depth, 0.0f, 1.0f));
    m_time.reset(clampTo(m_p.delayMs, 0.1f, 30.0f));
    m_regen.reset(clampTo(m_p.feedback, -0.85f, 0.85f));
    m_mix.reset(clampTo(m_p.mix, 0.0f, 1.0f));
    m_stereo.reset(clampTo(m_p.stereoPhase, 0.0f, 0.5f));
}
void Modulation::process(const AudioBuffer &buf) noexcept
{
    if (!buf.valid() || m_length == 0) return;
    constexpr double pi = 3.14159265358979323846;
    const int channels = std::min(buf.channelCount, m_channels);
    for (int i = 0; i < buf.frames; ++i) {
        const float speed = m_speed.next(), depth = m_depth.next();
        const float time = m_time.next(), feedback = m_regen.next();
        const float mix = m_mix.next(), stereo = m_stereo.next();
        for (int c = 0; c < channels; ++c) {
            if (!buf.channels[c]) continue;
            bool bad = false;
            const float dry = railed(buf.channels[c][i], &bad);
            const float lfo = float(std::sin(2.0 * pi * (m_phase + (c & 1) * stereo)));
            float wet = 0.0f;
            if (m_kind == Kind::Phaser) {
                const float hz = std::min(float(m_rate * 0.4), 700.0f * std::pow(2.0f, 2.5f * depth * lfo));
                const float t = float(std::tan(pi * hz / m_rate));
                const float a = (1.0f - t) / (1.0f + t);
                wet = dry + feedback * m_feedback[c];
                for (int s = 0; s < 6; ++s) {
                    const float y = -a * wet + m_ap[c][s];
                    m_ap[c][s] = wet + a * y;
                    wet = y;
                }
            } else {
                const float base = m_kind == Kind::Flanger ? std::min(time, 5.0f) : time;
                const float delay = clampTo(float(m_rate * 0.001) * base * (1.0f + 0.9f * depth * lfo), 1.0f, float(m_length - 2));
                float pos = float(m_write) - delay;
                if (pos < 0.0f) pos += float(m_length);
                const int a = int(pos), b = (a + 1) % m_length;
                const float f = pos - float(a);
                wet = m_delay[c][size_t(a)] * (1.0f - f) + m_delay[c][size_t(b)] * f;
                m_delay[c][size_t(m_write)] = railed(dry + feedback * wet, &bad);
            }
            m_feedback[c] = railed(wet, &bad);
            buf.channels[c][i] = mix == 0.0f ? dry : railed(dry + mix * (wet - dry), &bad);
            if (bad) { std::fill(std::begin(m_ap[c]), std::end(m_ap[c]), 0.0f); m_feedback[c] = 0.0f; }
        }
        m_write = (m_write + 1) % m_length;
        m_phase += speed / m_rate;
        if (m_phase >= 1.0) m_phase -= 1.0;
    }
}
} // namespace dreamdsp::dsp
