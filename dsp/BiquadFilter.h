#pragma once

#include "DspTypes.h"

namespace dreamdsp::dsp {

// A stateful biquad, direct form I. Deliberately separate from the app's
// core/Biquad: that one models Equalizer APO's filter vocabulary, this one is
// a building block for the effects and must not drag the DSP layer into a
// dependency on the application.
class BiquadFilter
{
public:
    enum class Kind { LowPass, HighPass, LowShelf, HighShelf, Peak, AllPass };

    void design(Kind kind, double freqHz, double q, double gainDb, double sampleRate);
    void reset() { m_x1 = m_x2 = m_y1 = m_y2 = 0.0f; }

    inline float process(float x)
    {
        const float y = m_b0 * x + m_b1 * m_x1 + m_b2 * m_x2 - m_a1 * m_y1 - m_a2 * m_y2;
        m_x2 = m_x1; m_x1 = x;
        m_y2 = m_y1; m_y1 = y;
        return y;
    }

    // Magnitude response, for tests and plotting.
    double magnitude(double freqHz, double sampleRate) const;

private:
    float m_b0 = 1.0f, m_b1 = 0.0f, m_b2 = 0.0f, m_a1 = 0.0f, m_a2 = 0.0f;
    float m_x1 = 0.0f, m_x2 = 0.0f, m_y1 = 0.0f, m_y2 = 0.0f;
};

// Linkwitz-Riley 4th order = two identical Butterworth sections in series.
// Its defining property is that the low and high outputs sum to an allpass:
// flat magnitude, which is what makes a multiband processor transparent when
// every band is left alone. That property is the crossover's unit test.
class LinkwitzRiley4
{
public:
    void design(double crossoverHz, double sampleRate);
    void reset();

    // Splits one sample into low and high.
    inline void process(float x, float *low, float *high)
    {
        *low = m_lp2.process(m_lp1.process(x));
        *high = m_hp2.process(m_hp1.process(x));
    }

private:
    BiquadFilter m_lp1, m_lp2, m_hp1, m_hp2;
};

// Removes the DC that asymmetric waveshaping introduces. Without it a
// saturation stage slowly walks the signal off centre and eats headroom.
class DcBlocker
{
public:
    void prepare(double sampleRate)
    {
        // ~5 Hz corner.
        m_r = float(1.0 - (2.0 * 3.14159265358979 * 5.0 / (sampleRate > 0 ? sampleRate : 48000.0)));
        reset();
    }
    void reset() { m_x1 = m_y1 = 0.0f; }

    inline float process(float x)
    {
        const float y = x - m_x1 + m_r * m_y1;
        m_x1 = x;
        m_y1 = y;
        return y;
    }

private:
    float m_r = 0.999f, m_x1 = 0.0f, m_y1 = 0.0f;
};

} // namespace dreamdsp::dsp
