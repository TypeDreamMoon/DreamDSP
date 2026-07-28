#pragma once

#include "BiquadFilter.h"

#include <functional>

namespace dreamdsp::dsp {

// 2x oversampling around a nonlinearity.
//
// This is not optional decoration. A waveshaper generates harmonics; any that
// land above Nyquist fold back as inharmonic tones, and unlike ordinary
// distortion they do not track the note -- they sound like ring modulation and
// get worse the brighter the material. Running the nonlinearity at twice the
// rate pushes the first fold-back an octave up, where the anti-aliasing filter
// can remove it.
//
// The filters are 8th-order Butterworth rather than a linear-phase FIR: phase
// response does not matter for a saturation stage, and the IIR costs a
// fraction of the taps.
class Oversampler2x
{
public:
    void prepare(double sampleRate);
    void reset();

    // Calls `shape` once per oversampled sample.
    float process(float x, const std::function<float(float)> &shape);

private:
    static constexpr int kStages = 4;   // 4 biquads = 8th order

    BiquadFilter m_up[kStages];
    BiquadFilter m_down[kStages];
    bool m_ready = false;
};

} // namespace dreamdsp::dsp
