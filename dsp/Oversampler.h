#pragma once

#include "BiquadFilter.h"

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

    // Calls `shape` twice per input sample -- once per oversampled sample.
    //
    // A template rather than a std::function parameter, and defined here so it
    // inlines. The callers pass small lambdas capturing two or three floats;
    // MSVC's std::function stores at most 16 bytes inline, so one more captured
    // float would have turned this into an operator new *per sample*, on the
    // real-time thread inside audiodg. Even below that threshold it cost an
    // indirect call for every oversampled sample.
    template <class F>
    float process(float x, F &&shape)
    {
        if (!m_ready)
            return shape(x);

        // Zero-stuffing halves the amplitude, so the interpolation filter's
        // output is scaled back up by two.
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

    // Runs only the interpolation half, handing `f` both oversampled phases.
    //
    // For measuring rather than processing: an inter-sample peak detector needs
    // to see the reconstructed waveform, and the reconstruction *is* this
    // filter, but it has no use for the decimated result. Skipping the
    // decimation half halves the cost.
    //
    // Shares filter state with process(), so one instance does one job. Nest
    // two of these -- the inner one prepared at twice the outer's rate -- for
    // the 4x that ITU-R BS.1770 specifies for true-peak measurement.
    template <class F>
    void tap(float x, F &&f)
    {
        if (!m_ready) {
            f(x);
            return;
        }
        float a = x * 2.0f;
        for (int i = 0; i < kStages; ++i)
            a = m_up[i].process(a);
        f(a);

        float b = 0.0f;
        for (int i = 0; i < kStages; ++i)
            b = m_up[i].process(b);
        f(b);
    }

private:
    static constexpr int kStages = 4;   // 4 biquads = 8th order

    BiquadFilter m_up[kStages];
    BiquadFilter m_down[kStages];
    bool m_ready = false;
};

} // namespace dreamdsp::dsp
