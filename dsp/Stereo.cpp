#include "Stereo.h"

#include <algorithm>
#include <cmath>

namespace dreamdsp::dsp {

// ---------------------------------------------------------------- StereoWidener

void StereoWidener::prepare(double sampleRate)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

    // 1 ms between taps. Zotter and Frank report that phase-based widening
    // works better at small tap spacings, and the spacing is also what the
    // causality delay is proportional to -- so the two pressures point the same
    // way. 1 ms is the compromise: 2 ms of latency, and a comb period low
    // enough to reach the frequencies where phantom-image width is decided.
    m_tap = clampTo(int(m_sampleRate * 0.001 + 0.5), 1, kMaxTap);
    m_lineLen = 4 * m_tap + 1;
    m_midLine.assign(size_t(m_lineLen), 0.0f);
    m_sideLine.assign(size_t(m_lineLen), 0.0f);
    m_write = 0;

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

    // The paper's series expansion of the widening angle. v is the one control;
    // everything else follows from it, which is what keeps this a single
    // bounded scalar on the wire rather than a set of coefficients.
    const float amount = clampTo(m_p.phaseAmount, 0.0f, 1.0f);
    const float v = amount * 0.7f;      // ICCC = J0(2v): 1.0 at v=0, ~0.55 at 0.7
    m_g0 = 1.0f - v * v * 0.25f;
    m_g1 = v * 0.5f - v * v * v / 16.0f;
    m_g2 = v * v * 0.125f;
    m_phaseActive = amount > 0.0f && m_lineLen > 0;
}

void StereoWidener::reset()
{
    m_splitS.reset();
    std::fill(m_midLine.begin(), m_midLine.end(), 0.0f);
    std::fill(m_sideLine.begin(), m_sideLine.end(), 0.0f);
    m_write = 0;
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

        if (!m_phaseActive) {
            L[i] = mid + side;
            R[i] = mid - side;
            continue;
        }

        // Zotter-Frank, written causally: the published filter spans z^+2N to
        // z^-2N, so every term is shifted down by 2N. The common part is what
        // survives a mono fold; the differential part is equal and opposite in
        // the two channels and therefore cancels exactly in the sum. That is
        // the whole mono-safety argument, and it holds for any g1 at all.
        m_midLine[size_t(m_write)] = mid;
        m_sideLine[size_t(m_write)] = side;

        const int n = m_tap;
        const auto at = [&](const std::vector<float> &line, int back) -> float {
            int idx = m_write - back;
            if (idx < 0)
                idx += m_lineLen;
            return line[size_t(idx)];
        };

        const float common = m_g0 * at(m_midLine, 2 * n)
                             + m_g2 * (at(m_midLine, 0) + at(m_midLine, 4 * n));
        const float diff = m_g1 * (at(m_midLine, n) - at(m_midLine, 3 * n));
        // The untouched side path is delayed to meet the widened mid, or the
        // two would be a comb filter of each other.
        const float sideDelayed = at(m_sideLine, 2 * n);

        L[i] = common + diff + sideDelayed;
        R[i] = common - diff - sideDelayed;

        if (++m_write >= m_lineLen)
            m_write = 0;
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
