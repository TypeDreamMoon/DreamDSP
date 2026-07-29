#include "MultibandCompressor.h"

#include <algorithm>

namespace dreamdsp::dsp {

void MultibandCompressor::prepare(double sampleRate, int channels, int maxFrames)
{
    m_sampleRate = sampleRate;
    m_channels = clampTo(channels, 1, kMaxChannels);
    m_maxFrames = maxFrames < 1 ? 1 : maxFrames;
    for (int b = 0; b < kBands; ++b) {
        m_comp[b].prepare(sampleRate, m_channels);
        m_scratch[b].assign(size_t(m_channels) * size_t(m_maxFrames), 0.0f);
    }
    setParams(m_p);
    // setParams used to reset the filter states as a side effect of designing
    // them. It no longer does, so the reset has to be asked for.
    reset();
}

void MultibandCompressor::setParams(const Params &p)
{
    m_p = p;
    const double lo = clampTo(double(m_p.lowCrossHz), 40.0, 1000.0);
    const double hi = clampTo(double(m_p.highCrossHz), lo * 1.5, 16000.0);

    constexpr double kQ = 0.70710678118654752;
    for (int c = 0; c < m_channels; ++c) {
        m_splitLow[c].design(lo, m_sampleRate);
        m_splitHigh[c].design(hi, m_sampleRate);
        // A 4th-order LR crossover is a cascade of two allpass-equivalent
        // sections; the band that bypasses it must be delayed identically.
        m_lowAllpass1[c].design(BiquadFilter::Kind::AllPass, hi, kQ, 0.0, m_sampleRate);
        m_lowAllpass2[c].design(BiquadFilter::Kind::AllPass, hi, kQ, 0.0, m_sampleRate);
    }

    for (int b = 0; b < kBands; ++b) {
        m_comp[b].setParams(m_p.band[b]);
        m_bandGainLin[b] = dbToLin(m_p.bandGainDb[b]);
    }
}

void MultibandCompressor::reset()
{
    for (int c = 0; c < m_channels; ++c) {
        m_splitLow[c].reset();
        m_splitHigh[c].reset();
        m_lowAllpass1[c].reset();
        m_lowAllpass2[c].reset();
    }
    for (int b = 0; b < kBands; ++b)
        m_comp[b].reset();
}

float MultibandCompressor::gainReductionDb(int band) const
{
    if (band < 0 || band >= kBands)
        return 0.0f;
    return m_comp[band].gainReductionDb();
}

void MultibandCompressor::process(const AudioBuffer &buf)
{
    if (!buf.valid())
        return;

    const int channels = std::min(buf.channelCount, m_channels);
    const int frames = buf.frames;

    // The scratch was sized in prepare(). Refuse rather than resize: this runs
    // on a real-time thread, and a caller handing over more than it declared is
    // a bug worth failing loudly in testing rather than allocating around.
    if (size_t(frames) * size_t(channels) > m_scratch[0].size())
        return;

    // --- split ------------------------------------------------------------
    for (int c = 0; c < channels; ++c) {
        const float *in = buf.channels[c];
        for (int i = 0; i < frames; ++i) {
            float low = 0.0f, rest = 0.0f;
            m_splitLow[c].process(in[i], &low, &rest);

            float mid = 0.0f, high = 0.0f;
            m_splitHigh[c].process(rest, &mid, &high);

            // Phase-match the low band against the second crossover.
            low = m_lowAllpass2[c].process(m_lowAllpass1[c].process(low));

            m_scratch[0][size_t(c) * size_t(frames) + size_t(i)] = low;
            m_scratch[1][size_t(c) * size_t(frames) + size_t(i)] = mid;
            m_scratch[2][size_t(c) * size_t(frames) + size_t(i)] = high;
        }
    }

    // --- compress each band independently ----------------------------------
    for (int b = 0; b < kBands; ++b) {
        if (!m_p.bandEnabled[b])
            continue;
        float *ptrs[kMaxChannels];
        for (int c = 0; c < channels; ++c)
            ptrs[c] = m_scratch[b].data() + size_t(c) * size_t(frames);
        AudioBuffer bandBuf{ ptrs, channels, frames };
        m_comp[b].process(bandBuf);
    }

    // --- sum ---------------------------------------------------------------
    for (int c = 0; c < channels; ++c) {
        float *out = buf.channels[c];
        for (int i = 0; i < frames; ++i) {
            float sum = 0.0f;
            // Every band is summed, including one whose compressor is disabled:
            // bandEnabled means "do not compress this band", not "mute it".
            for (int b = 0; b < kBands; ++b) {
                sum += m_scratch[b][size_t(c) * size_t(frames) + size_t(i)]
                       * m_bandGainLin[b];
            }
            out[i] = sum;
        }
    }
}

} // namespace dreamdsp::dsp
