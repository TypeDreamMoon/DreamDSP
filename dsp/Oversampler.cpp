#include "Oversampler.h"

namespace dreamdsp::dsp {

namespace {
// Butterworth Q values for an 8th-order cascade of four biquads.
constexpr double kQ[4] = { 0.50979558, 0.60134489, 0.89997622, 2.5629154 };
}

void Oversampler2x::prepare(double sampleRate)
{
    // Cutoff just below the original Nyquist, at the doubled rate.
    const double osRate = sampleRate * 2.0;
    const double cutoff = sampleRate * 0.45;

    for (int i = 0; i < kStages; ++i) {
        m_up[i].design(BiquadFilter::Kind::LowPass, cutoff, kQ[i], 0.0, osRate);
        m_down[i].design(BiquadFilter::Kind::LowPass, cutoff, kQ[i], 0.0, osRate);
    }
    m_ready = true;
    reset();
}

void Oversampler2x::reset()
{
    for (int i = 0; i < kStages; ++i) {
        m_up[i].reset();
        m_down[i].reset();
    }
}

float Oversampler2x::process(float x, const std::function<float(float)> &shape)
{
    if (!m_ready)
        return shape(x);

    // Zero-stuffing halves the amplitude, so the interpolation filter's output
    // is scaled back up by two.
    float a = x * 2.0f;
    for (int i = 0; i < kStages; ++i)
        a = m_up[i].process(a);

    float b = 0.0f;
    for (int i = 0; i < kStages; ++i)
        b = m_up[i].process(b);

    a = shape(a);
    b = shape(b);

    // Filter both, keep the first: that is the decimation.
    for (int i = 0; i < kStages; ++i)
        a = m_down[i].process(a);
    for (int i = 0; i < kStages; ++i)
        b = m_down[i].process(b);

    return a;
}

} // namespace dreamdsp::dsp
