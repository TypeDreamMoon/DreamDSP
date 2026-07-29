#include "Stereo.h"

#include <algorithm>
#include <cmath>

namespace dreamdsp::dsp {

// ---------------------------------------------------------------- StereoWidener

void StereoWidener::prepare(double sampleRate)
{
    m_sampleRate = sampleRate;
    setParams(m_p);
    // The crossover state used to be cleared as a side effect of designing it.
    // LinkwitzRiley4::design no longer does that -- it would click on every
    // parameter change -- so preparing has to ask.
    m_splitS.reset();
}

void StereoWidener::setParams(const Params &p)
{
    m_p = p;
    const double f = clampTo(double(m_p.monoBelowHz), 20.0, 500.0);
    m_splitS.design(f, m_sampleRate);
}

void StereoWidener::reset()
{
    m_splitS.reset();
}

void StereoWidener::process(const AudioBuffer &buf)
{
    if (!buf.valid() || buf.channelCount < 2)
        return;

    const float width = clampTo(m_p.width, 0.0f, 2.0f);
    const bool monoBass = m_p.monoBelowHz > 0.0f;

    float *L = buf.channels[0];
    float *R = buf.channels[1];

    for (int i = 0; i < buf.frames; ++i) {
        const float mid = (L[i] + R[i]) * 0.5f;
        float side = (L[i] - R[i]) * 0.5f;

        if (monoBass) {
            // Only the part of the side signal above the corner is widened;
            // what is below it is discarded, which is what "mono bass" means.
            float lowS = 0.0f, highS = 0.0f;
            m_splitS.process(side, &lowS, &highS);
            side = highS * width;
        } else {
            side *= width;
        }

        L[i] = mid + side;
        R[i] = mid - side;
    }
}

// -------------------------------------------------------------------- Crossfeed

void Crossfeed::prepare(double sampleRate)
{
    m_sampleRate = sampleRate;
    const int maxDelay = std::max(4, int(sampleRate * 0.002));   // 2 ms ceiling
    m_delayL.assign(size_t(maxDelay), 0.0f);
    m_delayR.assign(size_t(maxDelay), 0.0f);
    m_writeIndex = 0;
    setParams(m_p);
}

void Crossfeed::setParams(const Params &p)
{
    m_p = p;
    constexpr double kQ = 0.70710678118654752;
    m_lpL.design(BiquadFilter::Kind::LowPass, clampTo(double(m_p.cutoffHz), 100.0, 2000.0),
                 kQ, 0.0, m_sampleRate);
    m_lpR.design(BiquadFilter::Kind::LowPass, clampTo(double(m_p.cutoffHz), 100.0, 2000.0),
                 kQ, 0.0, m_sampleRate);
    m_feed = dbToLin(clampTo(m_p.feedDb, -24.0f, 0.0f));
    m_delaySamples = clampTo(int(m_p.delayUs * 1e-6f * float(m_sampleRate)),
                             0, int(m_delayL.size()) - 1);
}

void Crossfeed::reset()
{
    std::fill(m_delayL.begin(), m_delayL.end(), 0.0f);
    std::fill(m_delayR.begin(), m_delayR.end(), 0.0f);
    m_writeIndex = 0;
    m_lpL.reset();
    m_lpR.reset();
}

void Crossfeed::process(const AudioBuffer &buf)
{
    if (!buf.valid() || buf.channelCount < 2 || m_delayL.empty())
        return;

    const int size = int(m_delayL.size());
    float *L = buf.channels[0];
    float *R = buf.channels[1];

    // Normalised so the total energy does not climb with the feed level. m_feed
    // cannot change inside the loop, so this is hoisted out of it.
    const float norm = 1.0f / (1.0f + m_feed);

    for (int i = 0; i < buf.frames; ++i) {
        const float inL = L[i], inR = R[i];

        const int readIndex = (m_writeIndex - m_delaySamples + size) % size;
        const float delayedL = m_delayL[size_t(readIndex)];
        const float delayedR = m_delayR[size_t(readIndex)];

        m_delayL[size_t(m_writeIndex)] = inL;
        m_delayR[size_t(m_writeIndex)] = inR;
        if (++m_writeIndex >= size)
            m_writeIndex = 0;

        // Each ear also hears the opposite channel, later and duller.
        const float crossToL = m_lpR.process(delayedR) * m_feed;
        const float crossToR = m_lpL.process(delayedL) * m_feed;

        L[i] = (inL + crossToL) * norm;
        R[i] = (inR + crossToR) * norm;
    }
}

} // namespace dreamdsp::dsp
