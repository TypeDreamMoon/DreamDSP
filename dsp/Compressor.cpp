#include "Compressor.h"

#include <algorithm>

namespace dreamdsp::dsp {

namespace {
// Crest factor is measured over roughly this window; long enough to describe
// "the material", short enough to follow a change of instrument.
constexpr float kCrestWindowMs = 200.0f;
}

void Compressor::prepare(double sampleRate, int maxChannels)
{
    (void)maxChannels;
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    m_crestCoeff = timeConstant(kCrestWindowMs, m_sampleRate);
    reset();
}

void Compressor::reset()
{
    m_envelopeDb = 0.0f;
    m_peakSq = 0.0f;
    m_rmsSq = 0.0f;
    m_adaptiveAttackMs = m_p.attackMs;
    m_adaptiveReleaseMs = m_p.releaseMs;
    m_attackCoeff = timeConstant(m_p.attackMs, m_sampleRate);
    m_releaseCoeff = timeConstant(m_p.releaseMs, m_sampleRate);
}

float Compressor::effectiveKneeDb() const
{
    if (!m_p.autoKnee)
        return std::max(0.0f, m_p.kneeDb);
    // Gentle ratios can afford a wide knee; near-limiting ratios need a sharp
    // one or the "limit" never actually arrives.
    return clampTo(24.0f / std::max(1.0f, m_p.ratio), 1.0f, 12.0f);
}

float Compressor::computeReductionDb(float inputDb) const
{
    const float ratio = std::max(1.0f, m_p.ratio);
    if (ratio <= 1.0f)
        return 0.0f;

    const float knee = effectiveKneeDb();
    const float over = inputDb - m_p.thresholdDb;

    // Below the knee: untouched. Inside it: quadratic interpolation. Above:
    // the straight ratio line. This is the standard soft-knee gain computer.
    if (knee > 0.0f && over < -knee * 0.5f)
        return 0.0f;
    if (knee > 0.0f && over <= knee * 0.5f) {
        const float x = over + knee * 0.5f;
        return (1.0f / ratio - 1.0f) * x * x / (2.0f * knee) * -1.0f;
    }
    if (knee <= 0.0f && over <= 0.0f)
        return 0.0f;

    return over - (over / ratio);
}

float Compressor::effectiveMakeupDb() const
{
    if (!m_p.autoMakeup)
        return m_p.makeupDb;
    // Put the loudest possible signal back where it started: the reduction
    // applied at 0 dBFS is exactly what has to be given back.
    return computeReductionDb(0.0f);
}

float Compressor::outputForInputDb(float inputDb) const
{
    return inputDb - computeReductionDb(inputDb) + effectiveMakeupDb();
}

void Compressor::updateAdaptiveTimes(float inputDb)
{
    if (!m_p.autoAttack && !m_p.autoRelease)
        return;

    const float lin = dbToLin(inputDb);
    const float sq = lin * lin;
    m_peakSq = std::max(sq, m_peakSq * m_crestCoeff);
    m_rmsSq = sq + m_crestCoeff * (m_rmsSq - sq);

    // crest = peak / rms, ~1.4 for a sine, 4+ for percussion.
    const float crest = (m_rmsSq > 1e-12f)
                            ? std::sqrt(m_peakSq / m_rmsSq)
                            : 1.414f;
    const float c2 = clampTo(crest * crest, 2.0f, 64.0f);

    // Giannoulis' adaptive scheme: times scale inversely with crest factor, so
    // transient material gets short constants and sustained material long ones.
    if (m_p.autoAttack) {
        m_adaptiveAttackMs = clampTo(2.0f * (m_p.attackMs > 0 ? m_p.attackMs : 10.0f) / c2 * 8.0f,
                                     0.1f, 120.0f);
    }
    if (m_p.autoRelease) {
        m_adaptiveReleaseMs = clampTo(2.0f * (m_p.releaseMs > 0 ? m_p.releaseMs : 100.0f) / c2 * 8.0f,
                                      5.0f, 2000.0f);
    }
}

void Compressor::process(const AudioBuffer &buf)
{
    if (!buf.valid())
        return;

    const float ratio = std::max(1.0f, m_p.ratio);
    const float makeup = effectiveMakeupDb();
    const bool bypassGain = (ratio <= 1.0f);

    // Fixed-time path: coefficients computed once per block, not per sample.
    if (!m_p.autoAttack)
        m_attackCoeff = timeConstant(m_p.attackMs, m_sampleRate);
    if (!m_p.autoRelease)
        m_releaseCoeff = timeConstant(m_p.releaseMs, m_sampleRate);

    for (int i = 0; i < buf.frames; ++i) {
        // --- detector: stereo-linked peak ---------------------------------
        float peak = 0.0f;
        for (int c = 0; c < buf.channelCount; ++c)
            peak = std::max(peak, std::fabs(buf.channels[c][i]));

        const float inputDb = linToDb(peak);

        if (m_p.autoAttack || m_p.autoRelease) {
            updateAdaptiveTimes(inputDb);
            if (m_p.autoAttack)
                m_attackCoeff = timeConstant(m_adaptiveAttackMs, m_sampleRate);
            if (m_p.autoRelease)
                m_releaseCoeff = timeConstant(m_adaptiveReleaseMs, m_sampleRate);
        }

        // --- gain computer -------------------------------------------------
        const float targetDb = bypassGain ? 0.0f : computeReductionDb(inputDb);

        // --- branching smoother --------------------------------------------
        // Attack when reduction is increasing, release when it is backing off.
        // Smoothing the *reduction* rather than the level is what makes the
        // knee behave as designed.
        const float coeff = (targetDb > m_envelopeDb) ? m_attackCoeff : m_releaseCoeff;
        m_envelopeDb = targetDb + coeff * (m_envelopeDb - targetDb);

        // --- apply ----------------------------------------------------------
        const float gain = dbToLin(makeup - m_envelopeDb);
        for (int c = 0; c < buf.channelCount; ++c)
            buf.channels[c][i] *= gain;
    }
}

} // namespace dreamdsp::dsp
