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
            const float dry = ch[i];
            float wet = m_os[c].process(dry, [drive, bias](float v) {
                return tubeShape(v, drive, bias);
            });
            wet = m_dc[c].process(wet) * norm;
            ch[i] = (dry * (1.0f - mix) + wet * mix) * outGain;
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
        m_dc[c].prepare(sampleRate);
    }
    setParams(m_p);
}

void Exciter::setParams(const Params &p)
{
    m_p = p;
    constexpr double kQ = 0.70710678118654752;
    for (int c = 0; c < m_channels; ++c) {
        m_hp1[c].design(BiquadFilter::Kind::HighPass, m_p.frequencyHz, kQ, 0.0, m_sampleRate);
        m_hp2[c].design(BiquadFilter::Kind::HighPass, m_p.frequencyHz, kQ, 0.0, m_sampleRate);
    }
}

void Exciter::reset()
{
    for (int c = 0; c < m_channels; ++c) {
        m_hp1[c].reset(); m_hp2[c].reset();
        m_os[c].reset();  m_dc[c].reset();
    }
}

void Exciter::process(const AudioBuffer &buf)
{
    if (!buf.valid())
        return;

    const float amount = clampTo(m_p.amount, 0.0f, 1.0f);
    const float drive = m_p.drive;

    const int channels = std::min(buf.channelCount, m_channels);
    for (int c = 0; c < channels; ++c) {
        float *ch = buf.channels[c];
        for (int i = 0; i < buf.frames; ++i) {
            const float dry = ch[i];
            const float high = m_hp2[c].process(m_hp1[c].process(dry));

            float shaped = m_os[c].process(high, [drive](float v) {
                return std::tanh(drive * v);
            });
            shaped = m_dc[c].process(shaped);

            // Added on top of the untouched signal, never replacing it.
            ch[i] = dry + shaped * amount;
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
    setParams(m_p);
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
        m_harmLp[c].design(BiquadFilter::Kind::LowPass, cutoff * 6.0, kQ, 0.0, m_sampleRate);

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
            float harm = m_os[c].process(low, [drive](float v) {
                return tubeShape(v, drive, 0.35f);
            });
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
