#include "Clipper.h"

#include <algorithm>
#include <cmath>

namespace dreamdsp::dsp {

void SoftClipper::prepare(double sampleRate, int channels)
{
    m_channels = clampTo(channels, 1, kMaxChannels);
    for (int c = 0; c < m_channels; ++c)
        m_os[c].prepare(sampleRate);
    setParams(m_p);
    reset();
}

void SoftClipper::reset() noexcept
{
    for (int c = 0; c < m_channels; ++c)
        m_os[c].reset();
    m_reductionDb = 0.0f;
}

void SoftClipper::setParams(const Params &p) noexcept
{
    m_p = p;
    m_drive = dbToLin(clampTo(m_p.driveDb, 0.0f, 24.0f));
    m_ceiling = dbToLin(clampTo(m_p.ceilingDb, -24.0f, 0.0f));

    // kneeDb is how far below the ceiling the bend starts, so the normalised
    // half-width W is 1 minus that ratio.
    const float kneeDb = clampTo(m_p.kneeDb, 0.0f, 6.0f);
    m_knee = 1.0f - dbToLin(-kneeDb);
}

void SoftClipper::process(const AudioBuffer &buf)
{
    if (!buf.valid())
        return;

    const float drive = m_drive;
    const float ceiling = m_ceiling;
    const float invCeiling = 1.0f / std::max(1e-6f, ceiling);
    const float w = m_knee;
    const bool os = m_p.oversample;

    float peakIn = 0.0f, peakOut = 0.0f;

    const int channels = std::min(buf.channelCount, m_channels);
    for (int c = 0; c < channels; ++c) {
        float *ch = buf.channels[c];
        for (int i = 0; i < buf.frames; ++i) {
            const float x = ch[i] * drive;
            peakIn = std::max(peakIn, std::fabs(x));

            // Normalise to the ceiling, shape, scale back. Doing it this way
            // rather than folding the ceiling into the curve keeps the shape
            // one function with one parameter, which is what the self-test
            // checks against the closed form.
            float y;
            if (os) {
                y = m_os[c].process(x * invCeiling,
                                    [w](float v) { return clipShape(v, w); })
                    * ceiling;
                // Deliberately NOT clamped back to the ceiling here.
                //
                // The decimation filter's step response rings past the shaper's
                // output -- measured at 1.10 dB with 18 dB of drive. Clamping
                // that off is a hard clip at the base rate, which is precisely
                // the thing the oversampler was added to avoid: measured, it
                // costs 17.4 dB of alias suppression (82.6 dB down to 65.2).
                // Buying back one decibel of peak by throwing away seventeen
                // decibels of aliasing is a bad trade, and it is the wrong
                // stage to make it in -- the true-peak limiter downstream
                // bounds the output for real, and does it without folding
                // anything into the midrange.
            } else {
                y = clipShape(x * invCeiling, w) * ceiling;
            }

            peakOut = std::max(peakOut, std::fabs(y));
            ch[i] = y;
        }
    }

    m_reductionDb = (peakIn > 1e-9f && peakOut > 1e-9f)
                        ? 20.0f * std::log10(peakOut / peakIn)
                        : 0.0f;
    if (m_reductionDb > 0.0f)
        m_reductionDb = 0.0f;
}

} // namespace dreamdsp::dsp
