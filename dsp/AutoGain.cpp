#include "AutoGain.h"

#include <algorithm>
#include <cmath>

namespace dreamdsp::dsp {

// ------------------------------------------------------- LoudnessLeveller

namespace {

// Orban's slowest AGC release, the one their manual describes as slow enough
// to "effectively freeze the operation of the AGC". Used inside the dead zone.
constexpr float kCreepDbPerSec = 0.5f;

// Turning down is protective and reads as correct; turning up raises the noise
// floor with the music and is what people hear as pumping. AC-3 and AC-4 use a
// 30:1 asymmetry between attack and release; four to one is the conservative
// end of that and keeps a fade-in from being chased.
constexpr float kUpRateDivisor = 4.0f;

} // namespace

void LoudnessLeveller::prepare(double sampleRate, int channels, int)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_meter.prepare(m_sampleRate, channels, 3000.0);
    setParams(m_p);
    reset();
}

void LoudnessLeveller::reset() noexcept
{
    m_meter.reset();
    m_gainDb = 0.0f;
    m_targetDb = 0.0f;
}

void LoudnessLeveller::setParams(const Params &p) noexcept
{
    m_p = p;
    const float rate = clampTo(m_p.rateDbPerSec, 0.25f, 20.0f);
    const float perSample = 1.0f / float(m_sampleRate);
    m_downStep = rate * perSample;
    m_upStep = (rate / kUpRateDivisor) * perSample;
    m_creepStep = kCreepDbPerSec * perSample;
}

void LoudnessLeveller::updateTarget() noexcept
{
    const double lufs = m_meter.lufs();
    const float gate = clampTo(m_p.gateLufs, -70.0f, -20.0f);
    if (!m_meter.ready() || lufs < double(gate)) {
        // Freeze. Not "decay to unity" -- that would ride the gain back up
        // during a quiet passage and then have to undo it on the next entry.
        m_targetDb = m_gainDb;
        return;
    }
    const float target = clampTo(m_p.targetLufs, -40.0f, -10.0f);
    const float maxGain = clampTo(m_p.maxGainDb, 0.0f, 25.0f);
    m_targetDb = clampTo(float(double(target) - lufs), -maxGain, maxGain);
}

void LoudnessLeveller::process(const AudioBuffer &buf)
{
    if (!buf.valid())
        return;

    // The meter reads what arrives, before this stage's own gain.
    if (m_meter.process(buf))
        updateTarget();

    const float halfWindow = clampTo(m_p.windowDb, 0.0f, 12.0f) * 0.5f;
    const int channels = buf.channelCount;

    for (int i = 0; i < buf.frames; ++i) {
        const float err = m_targetDb - m_gainDb;
        if (err != 0.0f) {
            // Inside the target zone the gain creeps; outside it, it moves at
            // the full rate. Without this the leveller keeps making small
            // corrections to material that was already even, and those small
            // corrections are exactly what "breathing" is.
            float step;
            if (std::fabs(err) <= halfWindow)
                step = m_creepStep;
            else
                step = err > 0.0f ? m_upStep : m_downStep;
            m_gainDb += err > 0.0f ? std::min(step, err) : std::max(-step, err);
        }
        const float g = dbToLin(m_gainDb);
        for (int c = 0; c < channels; ++c)
            buf.channels[c][i] *= g;
    }
}

// ----------------------------------------------------------- DynamicRange

namespace {

// ETSI TS 103 190-1 Table 161, in knot form.
//
// The standard states the curve as a set of break levels plus ratios. The same
// curve is a polyline through five points, which is both simpler to evaluate
// and simpler to check: the ratios fall out of the slopes, and the self-test
// asserts them.
//
// Levels are dB relative to the reference; gains are dB. Below the first knot
// the gain is constant at gMaxBoost, above the last it is constant at gMaxCut.
// Music Light has no early-cut section, which is expressed by putting its
// section knot on top of the null band's upper edge.
struct DrcKnots {
    float lMaxBoost, gMaxBoost;
    float l0Low;                 // gain 0 from here...
    float l0High;                // ...to here: the null band
    float lSectionCut, gSectionCut;
    float lMaxCut, gMaxCut;
    float tauAttackMs, tauReleaseMs, tauAttackFastMs, tauReleaseFastMs;
    float attackThresholdDb, releaseThresholdDb;
};

constexpr DrcKnots kProfiles[DynamicRange::kProfileCount] = {
    // None -- never evaluated, present so the enum indexes directly.
    {   0.0f,  0.0f,   0.0f,  0.0f,   0.0f,  0.0f,   0.0f,   0.0f,
        100.0f, 3000.0f, 10.0f, 1000.0f, 15.0f, 20.0f },
    // Film Standard: boost 2:1 to +6, null 0..+5, early cut 2:1, cut 20:1
    { -12.0f,  6.0f,   0.0f,  5.0f,  15.0f, -5.0f,  35.0f, -24.0f,
        100.0f, 3000.0f, 10.0f, 1000.0f, 15.0f, 20.0f },
    // Film Light: wider null band (20 dB), same 20:1 top
    { -22.0f,  6.0f, -10.0f, 10.0f,  20.0f, -5.0f,  40.0f, -24.0f,
        100.0f, 3000.0f, 10.0f, 1000.0f, 15.0f, 20.0f },
    // Music Standard: 12 dB of boost, and the slowest release of the five
    { -24.0f, 12.0f,   0.0f,  5.0f,  15.0f, -5.0f,  35.0f, -24.0f,
        100.0f, 10000.0f, 10.0f, 1000.0f, 15.0f, 20.0f },
    // Music Light: no early cut, and the cut side stays at 2:1 throughout
    { -34.0f, 12.0f, -10.0f, 10.0f,  10.0f,  0.0f,  40.0f, -15.0f,
        100.0f, 3000.0f, 10.0f, 1000.0f, 15.0f, 20.0f },
    // Speech: the most aggressive boost, 4.75:1, and the fastest release
    { -19.0f, 15.0f,   0.0f,  5.0f,  15.0f, -5.0f,  35.0f, -24.0f,
        100.0f, 1000.0f, 10.0f, 200.0f, 10.0f, 10.0f },
};

inline float lerpKnot(float x0, float y0, float x1, float y1, float x) noexcept
{
    if (x1 <= x0)
        return y1;
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

} // namespace

float DynamicRange::curveGainDb(int32_t profile, float levelDb) noexcept
{
    if (profile <= kProfileNone || profile >= kProfileCount)
        return 0.0f;
    const DrcKnots &k = kProfiles[profile];

    if (levelDb <= k.lMaxBoost)
        return k.gMaxBoost;
    if (levelDb < k.l0Low)
        return lerpKnot(k.lMaxBoost, k.gMaxBoost, k.l0Low, 0.0f, levelDb);
    if (levelDb <= k.l0High)
        return 0.0f;
    if (levelDb < k.lSectionCut)
        return lerpKnot(k.l0High, 0.0f, k.lSectionCut, k.gSectionCut, levelDb);
    if (levelDb < k.lMaxCut)
        return lerpKnot(k.lSectionCut, k.gSectionCut, k.lMaxCut, k.gMaxCut, levelDb);
    return k.gMaxCut;
}

void DynamicRange::prepare(double sampleRate, int channels)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_channels = clampTo(channels, 1, kMaxChannels);
    for (int c = 0; c < m_channels; ++c)
        m_k[c].prepare(m_sampleRate);
    setParams(m_p);
    reset();
}

void DynamicRange::reset() noexcept
{
    for (int c = 0; c < m_channels; ++c)
        m_k[c].reset();
    m_acc = 0.0;
    m_accCount = 0;
    m_levelDb = -60.0f;
    m_gainDb = 0.0f;
    m_gainLin = 1.0f;
    m_gainNow = 1.0f;
    m_gainStep = 0.0f;
}

void DynamicRange::setParams(const Params &p) noexcept
{
    m_p = p;
    m_p.profile = clampTo(m_p.profile, int32_t(kProfileNone), int32_t(kProfileCount - 1));
}

void DynamicRange::process(const AudioBuffer &buf)
{
    if (!buf.valid() || m_p.profile == kProfileNone)
        return;

    const DrcKnots &k = kProfiles[m_p.profile];
    const float reference = clampTo(m_p.referenceLufs, -40.0f, -10.0f);
    const float boost = clampTo(m_p.boost, 0.0f, 1.0f);
    const float cut = clampTo(m_p.cut, 0.0f, 1.0f);

    // The tick period, in milliseconds, which is what the time constants are
    // expressed against.
    const float tickMs = float(double(kTick) * 1000.0 / m_sampleRate);

    const int channels = std::min(buf.channelCount, m_channels);

    for (int i = 0; i < buf.frames; ++i) {
        double sum = 0.0;
        for (int c = 0; c < channels; ++c) {
            const double y = m_k[c].process(double(buf.channels[c][i]));
            sum += y * y;
        }
        m_acc += sum;

        if (++m_accCount >= kTick) {
            const double mean = m_acc / (double(m_accCount) * std::max(1, channels));
            const float lkfs = mean > 0.0 ? float(-0.691 + 10.0 * std::log10(mean)) : -120.0f;
            const float level = lkfs - reference;

            // The four-way time constant, exactly as AC-4 specifies it: fast
            // or slow, attack or release, chosen by how far the instantaneous
            // level has moved away from the smoothed one.
            const float delta = level - m_levelDb;
            float tau;
            if (delta > k.attackThresholdDb)      tau = k.tauAttackFastMs;
            else if (delta > 0.0f)                tau = k.tauAttackMs;
            else if (-delta <= k.releaseThresholdDb) tau = k.tauReleaseMs;
            else                                  tau = k.tauReleaseFastMs;

            // alpha = 2^(-T/tau). Note the base: in AC-4 these taus are
            // half-lives, not the 1/e time constants every compressor textbook
            // uses. Dropping them into an exp()-based smoother would run 1.443
            // times too fast.
            const float alpha = (tau > 0.0f) ? std::exp2(-tickMs / tau) : 0.0f;

            m_levelDb = alpha * m_levelDb + (1.0f - alpha) * level;

            float g = curveGainDb(m_p.profile, level);
            g *= (g > 0.0f) ? boost : cut;

            // Smoothed in dB, not in linear gain. The standard smooths the
            // linear gain, which makes a boost slew about three times faster in
            // dB than a cut of the same size for the same tau -- an asymmetry
            // that is an artefact of the domain rather than anything anyone
            // chose. Decibels are symmetric and predictable.
            m_gainDb = alpha * m_gainDb + (1.0f - alpha) * g;

            const float next = dbToLin(m_gainDb);
            m_gainStep = (next - m_gainNow) / float(kTick);
            m_gainLin = next;

            m_acc = 0.0;
            m_accCount = 0;
        }

        // Ramp across the tick rather than stepping at its boundary; a 1.3 ms
        // staircase on a gain is audible as a buzz at 750 Hz.
        m_gainNow += m_gainStep;
        for (int c = 0; c < channels; ++c)
            buf.channels[c][i] *= m_gainNow;
    }
}

} // namespace dreamdsp::dsp
