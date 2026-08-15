#include "BassBoost.h"

#include <cmath>
#include <cstring>

namespace dreamdsp::dsp {

void DynamicBass::prepare(double sampleRate, int channels)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_channels = clampTo(channels, 0, kMaxChannels);

    m_p = Params{};
    setParams(m_p);
    reset();
}

void DynamicBass::reset() noexcept
{
    for (int c = 0; c < kMaxChannels; ++c)
        m_split[c].reset();
    m_env = 0.0f;
    m_appliedDb = 0.0;
}

void DynamicBass::setParams(const Params &p) noexcept
{
    m_p = p;

    double cutoff = clampTo(double(p.cutoffHz), 30.0, 250.0);
    const double nyquist = m_sampleRate * 0.5;
    if (cutoff > nyquist * 0.45)
        cutoff = nyquist * 0.45;

    for (int c = 0; c < m_channels; ++c)
        m_split[c].design(cutoff, m_sampleRate);

    m_releaseCoef = timeConstant(p.releaseMs, m_sampleRate);
    m_maxLin = dbToLin(clampTo(p.maxGainDb, 0.0f, 24.0f));
}

void DynamicBass::process(const AudioBuffer &buf)
{
    if (!buf.valid())
        return;

    const int channels = buf.channelCount < m_channels ? buf.channelCount : m_channels;
    const int frames = buf.frames;
    if (channels <= 0)
        return;

    // Two passes over the block rather than one, because the gain has to be
    // derived from the loudest channel and then applied to all of them. Doing
    // it per channel would move the stereo image every time the boost changed.
    //
    // The band split has to happen in both passes or be stored; storing it
    // would mean a scratch buffer per channel, and the split is four biquads.
    // Recomputing is cheaper than the memory traffic, but it would also run the
    // filters twice and corrupt their state -- so the low band is kept and the
    // high band is reconstructed as (input - low), which an LR4 crossover
    // guarantees is exact only in sum, not per band. Hence: one pass, with the
    // gain lagging by one sample.
    for (int f = 0; f < frames; ++f) {
        float peak = 0.0f;
        float low[kMaxChannels];

        for (int c = 0; c < channels; ++c) {
            float *x = buf.channels[c];
            if (!x) {
                low[c] = 0.0f;
                continue;
            }
            float lo = 0.0f, hi = 0.0f;
            m_split[c].process(x[f], &lo, &hi);
            low[c] = lo;
            // The high band is not kept: summing lo back with (x - lo) would
            // undo the crossover's phase, so the output is built from the two
            // bands the splitter produced.
            x[f] = hi;
            const float a = lo < 0.0f ? -lo : lo;
            if (a > peak)
                peak = a;
        }

        // Envelope on the *input* low band, rising instantly and falling with
        // the release. That is what makes the gain min(maxGain, headroom)
        // exactly, with no feedback path and no lag: the boosted band is
        // env * gain <= env * (1 / env) = 1.
        m_env = peak > m_env ? peak : peak + (m_env - peak) * m_releaseCoef;

        // Headroom as a linear factor. The epsilon keeps silence from asking
        // for infinite gain; at 1e-6 the cap is +120 dB, well above m_maxLin.
        const float headroom = 1.0f / (m_env + 1e-6f);
        const float gain = headroom < m_maxLin ? headroom : m_maxLin;

        bool bad = false;
        for (int c = 0; c < channels; ++c) {
            float *x = buf.channels[c];
            if (!x)
                continue;
            x[f] = railed(x[f] + low[c] * gain, &bad);
        }
        if (bad) {
            reset();
            return;
        }

        if (f == frames - 1)
            m_appliedDb = 20.0 * std::log10(double(gain) + 1e-12);
    }
}

} // namespace dreamdsp::dsp
