#include "Transient.h"

#include <algorithm>
#include <cmath>

namespace dreamdsp::dsp {

namespace {

// The constants SPL would not publish, taken from the implementations that did.
constexpr float kFastAttackMs = 0.5f;    // Env1: sees the leading edge
constexpr float kAttackPairRelMs = 40.0f;  // shared by Env1 and Env2
constexpr float kSustainPairAtkMs = 20.0f; // shared by Env3 and Env4
constexpr float kFastReleaseMs = 20.0f;  // Env3: follows the decay down

// Calf's slew limit: a factor of four per millisecond, i.e. 12.04 dB/ms.
constexpr float kSlewDbPerMs = 12.04f;

// One-pole rise/fall coefficient for a time constant expressed as the time to
// reach 1 - 1/e of a step. timeConstant() returns the *retained* fraction.
inline float rate(float ms, double sr)
{
    return 1.0f - timeConstant(ms, sr);
}

} // namespace

void TransientShaper::prepare(double sampleRate, int channels)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_channels = clampTo(channels, 1, kMaxChannels);
    m_maxStepDb = kSlewDbPerMs / float(m_sampleRate * 0.001);
    m_ready = true;
    setParams(m_p);
    reset();
}

void TransientShaper::reset() noexcept
{
    // Deliberately not seeded with a number here -- see m_primed.
    m_primed = false;
    m_env1.y = m_env2.y = m_env3.y = m_env4.y = 0.0f;
    m_gainDb = 0.0f;
}

void TransientShaper::setParams(const Params &p) noexcept
{
    m_p = p;
    if (!m_ready)
        return;

    const double sr = m_sampleRate;
    const float slowAtk = clampTo(m_p.attackMs, 1.0f, 500.0f);
    const float slowRel = clampTo(m_p.releaseMs, 1.0f, 5000.0f);

    m_env1.aUp = rate(kFastAttackMs, sr);
    m_env1.aDown = rate(kAttackPairRelMs, sr);
    m_env2.aUp = rate(slowAtk, sr);
    m_env2.aDown = rate(kAttackPairRelMs, sr);

    m_env3.aUp = rate(kSustainPairAtkMs, sr);
    m_env3.aDown = rate(kFastReleaseMs, sr);
    m_env4.aUp = rate(kSustainPairAtkMs, sr);
    m_env4.aDown = rate(slowRel, sr);

    m_floorDb = clampTo(m_p.thresholdDb, -90.0f, -20.0f);
    m_attackAmount = clampTo(m_p.attack, -1.0f, 1.0f);
    m_sustainAmount = clampTo(m_p.sustain, -1.0f, 1.0f);
}

void TransientShaper::process(const AudioBuffer &buf)
{
    if (!buf.valid() || !m_ready)
        return;
    if (m_attackAmount == 0.0f && m_sustainAmount == 0.0f)
        return;

    const int channels = std::min(buf.channelCount, m_channels);
    // How many dB one unit of the knob is worth. The knobs are +/-1 so that the
    // wire stays bounded scalars; this is where they become decibels.
    constexpr float kAttackScale = 1.5f;
    constexpr float kSustainScale = 1.5f;
    const float floorDb = m_floorDb;

    for (int i = 0; i < buf.frames; ++i) {
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c)
            peak = std::max(peak, std::fabs(buf.channels[c][i]));

        // The floor is relative to the signal, not absolute -- and that is
        // load-bearing. A fixed dB floor is a cliff: scaling the input scales
        // every envelope except the floor, so near it the ratios stop being
        // ratios and the stage silently becomes level-dependent again. Measured
        // with a fixed floor, the same burst 20 dB apart drew gains 3.6 dB
        // apart. Sixty decibels below the sustained level is far enough down
        // that nothing audible is clamped, and it scales with the signal.
        //
        // The absolute floor underneath it exists only so that digital silence
        // cannot walk the followers down without bound.
        if (!m_primed) {
            // Nothing has arrived yet. Leaving the followers alone rather than
            // letting them chase a floor value matters: the first sample of a
            // burst is usually a zero crossing, and priming there would seed
            // every follower with the floor instead of with the signal --
            // which is the level dependence this stage exists not to have,
            // reintroduced at the one moment it has no history to hide it.
            if (peak <= 0.0f)
                continue;
            m_env1.y = m_env2.y = m_env3.y = m_env4.y = linToDb(peak);
            m_primed = true;
        }

        // linToDb's own epsilon bottoms out at -180 dB, so nothing below this
        // is reachable anyway.
        constexpr float kHardFloorDb = -180.0f;
        const float relFloor = std::max(m_env4.y - 60.0f, kHardFloorDb);
        const float db = std::max(linToDb(peak), relFloor);

        const float e1 = m_env1.process(db);
        const float e2 = m_env2.process(db);
        const float e3 = m_env3.process(db);
        const float e4 = m_env4.process(db);

        // Each path is one-sided. Without that, a positive ATTACK setting also
        // attenuates the decay -- because during a decay the fast envelope sits
        // below the slow one -- which contradicts SPL's "the two do not
        // influence each other" and measurably ducks the tail by 10 dB or more
        // a couple of hundred milliseconds after the hit.
        const float dAttack = std::max(0.0f, e1 - e2);
        const float dSustain = std::max(0.0f, e4 - e3);

        float target = m_attackAmount * kAttackScale * dAttack
                       + m_sustainAmount * kSustainScale * dSustain;
        target = clampTo(target, -kMaxGainDb, kMaxGainDb);

        // The gate, which is a different thing from the floor above and is
        // absolute on purpose. Level independence means this stage boosts a
        // -75 dBFS noise floor exactly as hard as a -30 dBFS one -- measured,
        // the gain range is identical -- so something has to say "that is room
        // tone, leave it alone", and only an absolute threshold can.
        if (e4 <= floorDb + 1.0f)
            target = 0.0f;

        const float step = clampTo(target - m_gainDb, -m_maxStepDb, m_maxStepDb);
        m_gainDb += step;

        const float g = dbToLin(m_gainDb);
        for (int c = 0; c < channels; ++c)
            buf.channels[c][i] *= g;
    }
}

} // namespace dreamdsp::dsp
