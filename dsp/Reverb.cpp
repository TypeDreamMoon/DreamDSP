#include "Reverb.h"

#include <algorithm>

namespace dreamdsp::dsp {

namespace {

// Freeverb's tuning, in samples at its design rate of 44.1 kHz. The comb
// lengths are mutually prime on purpose: shared factors make the modes line up
// and the tail ring on a pitch.
constexpr int kCombTuning[] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
constexpr int kAllpassTuning[] = { 556, 441, 341, 225 };

// The right channel's delays are offset so the two sides decorrelate.
constexpr int kStereoSpread = 23;

constexpr double kDesignRate = 44100.0;

// Freeverb's published scaling. `fixedGain` keeps the eight parallel combs
// from summing into clipping.
constexpr float kFixedGain = 0.015f;
constexpr float kScaleDamp = 0.4f;
constexpr float kScaleRoom = 0.28f;
constexpr float kOffsetRoom = 0.7f;

int scaled(int samples, double sampleRate)
{
    return std::max(1, int(double(samples) * sampleRate / kDesignRate + 0.5));
}

} // namespace

void Reverb::prepare(double sampleRate)
{
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;

    for (int i = 0; i < kCombs; ++i) {
        m_combL[i].setSize(scaled(kCombTuning[i], m_sampleRate));
        m_combR[i].setSize(scaled(kCombTuning[i] + kStereoSpread, m_sampleRate));
    }
    for (int i = 0; i < kAllpasses; ++i) {
        m_apL[i].setSize(scaled(kAllpassTuning[i], m_sampleRate));
        m_apR[i].setSize(scaled(kAllpassTuning[i] + kStereoSpread, m_sampleRate));
    }

    const int maxPre = int(m_sampleRate * 0.2) + 1;   // 200 ms ceiling
    m_preL.assign(size_t(maxPre), 0.0f);
    m_preR.assign(size_t(maxPre), 0.0f);
    m_preIndex = 0;

    updateInternal();
    reset();
}

void Reverb::reset()
{
    for (int i = 0; i < kCombs; ++i) {
        std::fill(m_combL[i].buf.begin(), m_combL[i].buf.end(), 0.0f);
        std::fill(m_combR[i].buf.begin(), m_combR[i].buf.end(), 0.0f);
        m_combL[i].store = m_combR[i].store = 0.0f;
        m_combL[i].index = m_combR[i].index = 0;
    }
    for (int i = 0; i < kAllpasses; ++i) {
        std::fill(m_apL[i].buf.begin(), m_apL[i].buf.end(), 0.0f);
        std::fill(m_apR[i].buf.begin(), m_apR[i].buf.end(), 0.0f);
        m_apL[i].index = m_apR[i].index = 0;
    }
    std::fill(m_preL.begin(), m_preL.end(), 0.0f);
    std::fill(m_preR.begin(), m_preR.end(), 0.0f);
    m_preIndex = 0;
    m_inLpL = m_inLpR = 0.0f;
}

void Reverb::updateInternal()
{
    const float room = clampTo(m_p.roomSize, 0.0f, 1.0f);
    const float damp = clampTo(m_p.damping, 0.0f, 1.0f);
    const float density = clampTo(m_p.density, 0.0f, 1.0f);
    const float bw = clampTo(m_p.bandwidth, 0.0f, 1.0f);
    const float width = clampTo(m_p.width, 0.0f, 1.0f);
    const float wet = clampTo(m_p.wet, 0.0f, 1.0f);

    m_feedback = room * kScaleRoom + kOffsetRoom;
    m_damp1 = damp * kScaleDamp;
    m_damp2 = 1.0f - m_damp1;

    // Freeverb fixes allpass feedback at 0.5; exposing it as "density" spans a
    // range that stays stable (an allpass is stable for |g| < 1).
    m_apFeedback = 0.3f + density * 0.4f;

    // One-pole lowpass on the input. bandwidth 1 = straight through.
    m_bwCoeff = (bw >= 0.999f) ? 0.0f : (1.0f - bw) * 0.85f;

    // Freeverb's stereo width: wet1 stays on its own side, wet2 crosses over.
    m_wet1 = wet * (width * 0.5f + 0.5f);
    m_wet2 = wet * ((1.0f - width) * 0.5f);

    m_preLen = clampTo(int(m_p.preDelayMs * 0.001f * float(m_sampleRate)),
                       0, int(m_preL.size()) - 1);
}

void Reverb::process(const AudioBuffer &buf)
{
    if (!buf.valid() || m_preL.empty())
        return;

    const bool stereo = buf.channelCount >= 2;
    const int preSize = int(m_preL.size());

    for (int n = 0; n < buf.frames; ++n) {
        const float inL = buf.channels[0][n];
        const float inR = stereo ? buf.channels[1][n] : inL;

        // --- pre-delay ----------------------------------------------------
        float preOutL = inL, preOutR = inR;
        if (m_preLen > 0) {
            const int readIndex = (m_preIndex - m_preLen + preSize) % preSize;
            preOutL = m_preL[size_t(readIndex)];
            preOutR = m_preR[size_t(readIndex)];
        }
        m_preL[size_t(m_preIndex)] = inL;
        m_preR[size_t(m_preIndex)] = inR;
        if (++m_preIndex >= preSize)
            m_preIndex = 0;

        // --- input bandwidth ----------------------------------------------
        if (m_bwCoeff > 0.0f) {
            m_inLpL = preOutL * (1.0f - m_bwCoeff) + m_inLpL * m_bwCoeff;
            m_inLpR = preOutR * (1.0f - m_bwCoeff) + m_inLpR * m_bwCoeff;
            preOutL = m_inLpL;
            preOutR = m_inLpR;
        }

        // Both halves are driven by the sum, which is what gives Freeverb its
        // dense mono-compatible tail.
        const float input = (preOutL + preOutR) * kFixedGain;

        // --- eight parallel combs ------------------------------------------
        float outL = 0.0f, outR = 0.0f;
        for (int i = 0; i < kCombs; ++i) {
            outL += m_combL[i].process(input, m_feedback, m_damp1, m_damp2);
            outR += m_combR[i].process(input, m_feedback, m_damp1, m_damp2);
        }

        // --- four series allpasses -----------------------------------------
        for (int i = 0; i < kAllpasses; ++i) {
            outL = m_apL[i].process(outL, m_apFeedback);
            outR = m_apR[i].process(outR, m_apFeedback);
        }

        // --- mix ------------------------------------------------------------
        const float wetL = outL * m_wet1 + outR * m_wet2;
        const float wetR = outR * m_wet1 + outL * m_wet2;

        if (stereo) {
            buf.channels[0][n] = wetL + inL * m_p.dry;
            buf.channels[1][n] = wetR + inR * m_p.dry;
            for (int c = 2; c < buf.channelCount; ++c)
                buf.channels[c][n] = buf.channels[c][n] * m_p.dry;
        } else {
            buf.channels[0][n] = (wetL + wetR) * 0.5f + inL * m_p.dry;
        }
    }
}

} // namespace dreamdsp::dsp
