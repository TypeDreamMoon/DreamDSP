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

// process() is a template and lives in the header, so it can inline into the
// three saturation stages that call it once per sample.

} // namespace dreamdsp::dsp
