#include "TapeSaturation.h"

#include <algorithm>

namespace dreamdsp::dsp {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kBumpR = 1.428571429f; // Q=0.7, broad head bump rather than a ringing resonance.

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

void TapeSaturation::Channel::reset() noexcept
{
    outer.reset(); inner.reset(); dc.reset();
    bandState = lowState = flux = envelope = toneState = 0.0f;
}

void TapeSaturation::prepare(double sampleRate, int channels)
{
    m_supported = false;
    m_channels = 0;
    m_started = false;
    if (!validRate(sampleRate) || channels < 1 || channels > kMaxChannels) return;
    m_rate = sampleRate;
    m_channels = channels;
    const double osRate = 4.0 * m_rate;
    m_bumpG = float(std::tan(kPi * 65.0 / osRate));
    m_bumpDenom = 1.0f / (1.0f + m_bumpG * (m_bumpG + kBumpR));
    m_fluxRate = 1.0f - timeConstant(0.15f, osRate);
    m_attackRate = 1.0f - timeConstant(5.0f, osRate);
    m_releaseRate = 1.0f - timeConstant(60.0f, osRate);
    for (int c = 0; c < m_channels; ++c) {
        m_channel[size_t(c)].outer.prepare(m_rate);
        m_channel[size_t(c)].inner.prepare(2.0 * m_rate);
        m_channel[size_t(c)].dc.prepare(m_rate);
    }
    for (auto *s : {&m_drive, &m_tone, &m_bump, &m_memory, &m_mix, &m_output}) s->prepare(m_rate);
    m_supported = true;
    setParams(m_p);
    reset();
}

void TapeSaturation::setParams(const Params &params) noexcept
{
    const Params defaults;
    m_p.driveDb = clean(params.driveDb, defaults.driveDb, 0, 24);
    m_p.toneHz = clean(params.toneHz, defaults.toneHz, 2000, 20000);
    m_p.bumpDb = clean(params.bumpDb, defaults.bumpDb, 0, 6);
    m_p.memory = clean(params.memory, defaults.memory, 0, 1);
    m_p.mix = clean(params.mix, 0, 0, 1);
    m_p.outputDb = clean(params.outputDb, 0, -24, 6);
    m_targetDrive = dbToLin(m_p.driveDb);
    m_targetBump = dbToLin(m_p.bumpDb);
    m_targetOutput = dbToLin(m_p.outputDb);
    const double toneHz = std::min(double(m_p.toneHz), m_rate * 0.45);
    const double g = std::tan(kPi * toneHz / (m_rate * 4.0));
    m_targetTone = float(g / (1.0 + g));
    m_drive.setTarget(m_targetDrive); m_tone.setTarget(m_targetTone);
    m_bump.setTarget(m_targetBump); m_memory.setTarget(m_p.memory);
    m_mix.setTarget(m_p.mix); m_output.setTarget(m_targetOutput);
    if (!m_started) {
        m_drive.reset(m_targetDrive); m_tone.reset(m_targetTone);
        m_bump.reset(m_targetBump); m_memory.reset(m_p.memory);
        m_mix.reset(m_p.mix); m_output.reset(m_targetOutput);
    }
}

void TapeSaturation::reset() noexcept
{
    for (auto &channel : m_channel) channel.reset();
    m_started = false;
    m_drive.reset(m_targetDrive); m_tone.reset(m_targetTone);
    m_bump.reset(m_targetBump); m_memory.reset(m_p.memory);
    m_mix.reset(m_p.mix); m_output.reset(m_targetOutput);
}

void TapeSaturation::process(const AudioBuffer &buffer) noexcept
{
    if (!m_supported || !buffer.valid()) return;
    const int channels = std::min(buffer.channelCount, m_channels);
    m_started = true;
    for (int i = 0; i < buffer.frames; ++i) {
        // One advance per frame, shared by every channel and all four phases.
        const float drive = m_drive.next(), tone = m_tone.next(), bump = m_bump.next();
        const float memory = m_memory.next(), mix = m_mix.next(), output = m_output.next();
        for (int c = 0; c < channels; ++c) {
            if (!buffer.channels[c]) continue;
            auto &state = m_channel[size_t(c)];
            bool bad = false;
            const float input = railed(buffer.channels[c][i], &bad);
            if (bad) state.reset();
            const float oversampled = state.outer.process(input, [&](float v0) {
                return state.inner.process(v0, [&](float v) {
                    // Topology-preserving state-variable bandpass: multiply by
                    // R to make its small-signal centre-frequency gain unity.
                    const float band = (state.bandState + m_bumpG * (v - state.lowState)) * m_bumpDenom;
                    const float low = state.lowState + m_bumpG * band;
                    state.bandState = 2.0f * band - state.bandState;
                    state.lowState = 2.0f * low - state.lowState;
                    const float head = v + (bump - 1.0f) * kBumpR * band;
                    const float driven = clampTo(drive * head, -64.0f, 64.0f);
                    const float peak = std::fabs(driven);
                    const float rate = peak > state.envelope ? m_attackRate : m_releaseRate;
                    state.envelope += rate * (peak - state.envelope);
                    const float sag = 1.0f / (1.0f + 0.35f * memory * state.envelope);
                    // Feedback gain <=0.2 and tanh's bounded output prevent
                    // runaway/remanent DC. This is a useful approximation,
                    // not the Jiles-Atherton physical hysteresis equation.
                    const float magnetic = std::tanh(driven * sag + 0.2f * memory * state.flux);
                    state.flux += m_fluxRate * (magnetic - state.flux);
                    const float delta = tone * (magnetic - state.toneState);
                    const float wet = state.toneState + delta;
                    state.toneState = wet + delta;
                    // Both dry and wet see exactly the same anti-imaging and
                    // decimation path; no direct/oversampled phase mismatch.
                    return v + mix * (wet - v);
                });
            });
            bool outputBad = false;
            buffer.channels[c][i] = railed(state.dc.process(oversampled) * output, &outputBad);
            if (outputBad) state.reset();
        }
    }
}
} // namespace dreamdsp::dsp
