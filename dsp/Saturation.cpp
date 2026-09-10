#include "Saturation.h"

#include <algorithm>
#include <cmath>

namespace dreamdsp::dsp {

float tubeShape(float x, float drive, float bias)
{
    // tanh with the operating point shifted. Subtracting tanh(bias) removes
    // most of the resulting DC offset immediately; the DC blocker downstream
    // handles what is left once the signal is asymmetric.
    const float d = std::max(0.1f, drive);
    return std::tanh(d * x + bias) - std::tanh(bias);
}

// ------------------------------------------------------------------ TubeStage

void TubeStage::prepare(double sampleRate, int channels)
{
    m_channels = clampTo(channels, 1, kMaxChannels);
    for (int c = 0; c < m_channels; ++c) {
        m_os[c].prepare(sampleRate);
        m_dc[c].prepare(sampleRate);
    }
}

void TubeStage::reset()
{
    for (int c = 0; c < m_channels; ++c) {
        m_os[c].reset();
        m_dc[c].reset();
    }
}

void TubeStage::process(const AudioBuffer &buf)
{
    if (!buf.valid())
        return;

    const float mix = clampTo(m_p.mix, 0.0f, 1.0f);
    const float outGain = dbToLin(m_p.outputDb);
    const float drive = m_p.drive;
    const float bias = m_p.bias;

    // Normalise so raising drive changes character, not just level -- otherwise
    // every A/B comparison is just "the louder one sounds better".
    const float norm = 1.0f / std::max(0.05f, std::tanh(drive) );

    const int channels = std::min(buf.channelCount, m_channels);
    for (int c = 0; c < channels; ++c) {
        float *ch = buf.channels[c];
        for (int i = 0; i < buf.frames; ++i) {
            // The blend happens *inside* the oversampler, at the doubled rate.
            //
            // Mixing an untouched dry against an oversampled wet comb-filters
            // the result: the up/down Butterworth pair delays the wet path by
            // about 1.28 samples, so at half wet the two cancel wherever that
            // is half a period -- measured as a 17.8 dB notch at 18.7 kHz.
            // Feeding both through the same filters costs the dry path the
            // oversampler's own roll-off (2.2 dB at 20 kHz, flat below 18 kHz)
            // and buys exact alignment at every mix setting. There is no third
            // option: the delay is not a constant, so no fixed compensating
            // delay on the dry path can cancel it across the band.
            const float y = m_os[c].process(ch[i], [=](float v) {
                return v * (1.0f - mix) + tubeShape(v, drive, bias) * norm * mix;
            });
            ch[i] = m_dc[c].process(y) * outGain;
        }
    }
}

// -------------------------------------------------------------------- Exciter

void Exciter::prepare(double sampleRate, int channels)
{
    m_sampleRate = sampleRate;
    m_channels = clampTo(channels, 1, kMaxChannels);
    for (int c = 0; c < m_channels; ++c) {
        m_os[c].prepare(sampleRate);
        // The inner stage runs at the outer one's internal rate, so it is
        // prepared with that rate and its own anti-imaging filters land an
        // octave higher again.
        m_os2[c].prepare(sampleRate * 2.0);
        m_dc[c].prepare(sampleRate);
    }
    setParams(m_p);
}

void Exciter::setParams(const Params &p)
{
    m_p = p;
    constexpr double kQ = 0.70710678118654752;
    // Four times the rate: these run in the innermost oversampled domain.
    // See process().
    const double osRate = m_sampleRate * 4.0;
    for (int c = 0; c < m_channels; ++c) {
        m_hp1[c].design(BiquadFilter::Kind::HighPass, m_p.frequencyHz, kQ, 0.0, osRate);
        m_hp2[c].design(BiquadFilter::Kind::HighPass, m_p.frequencyHz, kQ, 0.0, osRate);
    }
    m_threshold = dbToLin(clampTo(m_p.thresholdDb, -120.0f, -12.0f));
}

void Exciter::reset()
{
    for (int c = 0; c < m_channels; ++c) {
        m_hp1[c].reset(); m_hp2[c].reset();
        m_os[c].reset();  m_os2[c].reset();
        m_dc[c].reset();
    }
}

void Exciter::process(const AudioBuffer &buf)
{
    if (!buf.valid())
        return;

    const float amount = clampTo(m_p.amount, 0.0f, 1.0f);
    const float drive = m_p.drive;
    const float threshold = m_threshold;

    const int channels = std::min(buf.channelCount, m_channels);
    for (int c = 0; c < channels; ++c) {
        float *ch = buf.channels[c];
        for (int i = 0; i < buf.frames; ++i) {
            // Band split, shaper and sum all live inside the oversampler, for
            // the reason given in TubeStage::process. Here the stakes are
            // higher: below saturation the shaped band is very nearly a scaled
            // copy of the dry high band, so an unaligned sum is a deep comb --
            // at the default drive and amount the two very nearly cancel.
            //
            // Running the split at the doubled rate is not a compromise made
            // to fit; it is the better place for it either way.
            const float y = m_os[c].process(ch[i], [&](float v0) {
                return m_os2[c].process(v0, [&](float v) {
                    const float high = m_hp2[c].process(m_hp1[c].process(v));
                    // Centre clipping, which is what the threshold means: the
                    // shaper sees only the part of the band that sticks out
                    // above it, so sustained content passes through the wet
                    // path as silence rather than as steady distortion.
                    const float a = std::fabs(high) - threshold;
                    const float gated = a > 0.0f ? std::copysign(a, high) : 0.0f;
                    // Added on top of the untouched signal, never replacing it.
                    return v + std::tanh(drive * gated) * amount;
                });
            });
            ch[i] = m_dc[c].process(y);
        }
    }
}

// ---------------------------------------------------------------- VirtualBass

void VirtualBass::prepare(double sampleRate, int channels)
{
    m_sampleRate = sampleRate;
    m_channels = clampTo(channels, 1, kMaxChannels);
    for (int c = 0; c < m_channels; ++c) {
        m_os[c].prepare(sampleRate);
        m_dc[c].prepare(sampleRate);
    }
    // 120 ms: long enough that the normaliser does not chase individual cycles
    // of a 40 Hz note (which would flatten the note's own envelope), short
    // enough to follow a change of programme.
    // 60 ms. The residual ripple of a mean-square detector on a sine is about
    // 1/(4*pi*f*T), which at the 45 Hz bottom end of what this stage is for
    // works out at 3% and falls away above it -- while still settling fast
    // enough that a kick drum gets its harmonics on the first beat.
    m_msRate = 1.0f - timeConstant(60.0f, sampleRate);
    setParams(m_p);
    // The band splitter's state used to be cleared as a side effect of
    // designing it; LinkwitzRiley4::design no longer does that, because it
    // clicked on every parameter change.
    for (int c = 0; c < m_channels; ++c)
        m_split[c].reset();
}

void VirtualBass::setParams(const Params &p)
{
    m_p = p;
    constexpr double kQ = 0.70710678118654752;
    const double cutoff = clampTo(double(m_p.cutoffHz), 30.0, 300.0);

    for (int c = 0; c < m_channels; ++c) {
        m_split[c].design(cutoff, m_sampleRate);

        // Keep only the generated harmonics: everything below the cutoff is
        // exactly what the speaker cannot play, so putting it back is pointless.
        m_harmHp1[c].design(BiquadFilter::Kind::HighPass, cutoff, kQ, 0.0, m_sampleRate);
        m_harmHp2[c].design(BiquadFilter::Kind::HighPass, cutoff, kQ, 0.0, m_sampleRate);
        // Four times the cutoff, not six.
        //
        // The one controlled experiment on this parameter -- a MUSHRA test
        // with per-item ANOVA, run on harmonic count alone -- found two and
        // three synthesised harmonics indistinguishable, and a significant
        // quality loss on every programme item at four. Its authors write that
        // they "do not recommend using more than three harmonics if we want to
        // avoid slightly annoying distortion". Band-limiting to 4x the cutoff
        // keeps the 2nd, 3rd and 4th partials of the fundamental and rolls the
        // rest away; the 3rd to 5th are the ones the ear actually builds the
        // residue pitch from, so this is also where the perceptual work is.
        m_harmLp[c].design(BiquadFilter::Kind::LowPass, cutoff * 4.0, kQ, 0.0, m_sampleRate);

        // Optional: remove the un-reproducible lows from the dry path too,
        // which frees the driver's excursion for the harmonics.
        m_outHp1[c].design(BiquadFilter::Kind::HighPass, cutoff, kQ, 0.0, m_sampleRate);
        m_outHp2[c].design(BiquadFilter::Kind::HighPass, cutoff, kQ, 0.0, m_sampleRate);
    }
}

void VirtualBass::reset()
{
    for (int c = 0; c < m_channels; ++c) {
        m_split[c].reset();
        m_harmHp1[c].reset(); m_harmHp2[c].reset(); m_harmLp[c].reset();
        m_outHp1[c].reset(); m_outHp2[c].reset();
        m_os[c].reset();     m_dc[c].reset();
        m_ms[c] = 0.0f;
    }
}

void VirtualBass::process(const AudioBuffer &buf)
{
    if (!buf.valid())
        return;

    const float amount = clampTo(m_p.amount, 0.0f, 1.0f) * 2.0f;
    const float drive = m_p.drive;

    const int channels = std::min(buf.channelCount, m_channels);
    for (int c = 0; c < channels; ++c) {
        float *ch = buf.channels[c];
        for (int i = 0; i < buf.frames; ++i) {
            const float in = ch[i];

            float low = 0.0f, high = 0.0f;
            m_split[c].process(in, &low, &high);

            // Harmonics of the sub band. An asymmetric shaper, not tanh: tanh
            // is odd-symmetric and therefore produces only odd harmonics, and
            // the missing-fundamental illusion is far stronger when the second
            // is present too -- it is the first interval the ear reconstructs
            // the octave from.
            // Hold the shaper's operating point still. `kRef` is where the
            // curve is asked to work, and dividing back out afterwards means
            // the stage's *gain* is unchanged -- only the harmonic balance is
            // stabilised. Below the floor the scale stops growing, which
            // matters not because it would be loud (the shaper is linear down
            // there, so nothing is generated) but because an unbounded
            // reciprocal is an unbounded number.
            constexpr float kRef = 0.5f;
            constexpr float kEnvFloor = 1e-3f;   // -60 dBFS
            m_ms[c] += (low * low - m_ms[c]) * m_msRate;
            // sqrt(2) turns an RMS into the peak of the sine it came from,
            // which is the number the shaper's operating point is expressed in.
            const float env = 1.41421356f * std::sqrt(m_ms[c]);
            const float scale = kRef / std::max(env, kEnvFloor);

            float harm = m_os[c].process(low * scale, [drive](float v) {
                return tubeShape(v, drive, 0.35f);
            }) / scale;
            harm = m_dc[c].process(harm);
            harm = m_harmHp2[c].process(m_harmHp1[c].process(harm));
            harm = m_harmLp[c].process(harm);

            const float dry = m_p.removeOriginal
                                  ? m_outHp2[c].process(m_outHp1[c].process(in))
                                  : in;

            ch[i] = dry + harm * amount;
        }
    }
}

} // namespace dreamdsp::dsp
