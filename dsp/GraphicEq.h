#pragma once

#include "Equalizer.h"

namespace dreamdsp::dsp {

// A 31-band ISO third-octave graphic equalizer.
//
// This is what AutoEQ's `GraphicEQ: 20 -1.5; 25 -1.4; ...` lines and JamesDSP's
// arbitrary-magnitude equalizer are for: a correction described as a curve
// rather than as filters, because the curve did not come from filters.
//
// Realised as a cascade of peaking sections on fixed centre frequencies rather
// than as an FIR, which is the other obvious choice and the one JamesDSP and
// Equalizer APO both make. The reasons are specific:
//
//   * An FIR needs a convolver, and a partitioned convolver's latency is its
//     block size -- several milliseconds added to every stream, for an
//     equalizer. This has none.
//   * Only bounded gains travel on the wire. Filter coefficients cannot be
//     validated (r = 0.998 is both a 40 Hz highpass at 192 kHz and a +48 dB
//     resonator), and this runs inside audiodg.exe reading a file that any
//     user can write. Thirty-one clamped decibel values can be bounded exactly.
//   * The design is designFilter(), which is already the audio path and is
//     already tested against measured impulse responses.
//
// The cost is resolution: third-octave, against an FIR's ability to follow a
// curve as sharply as its length allows. For smoothed headphone corrections --
// which is what these curves are -- that is not the binding constraint, and
// anything genuinely sharp belongs in the parametric bands next door.
class GraphicEq
{
public:
    static constexpr int kBands = 31;

    // Low enough that adjacent bands overlap and the result is smooth rather
    // than rippled, which is what makes the fit below converge at all. A
    // textbook third-octave Q of 4.3 leaves visible scallops between centres.
    static constexpr double kQ = 2.2;

    // The ISO 266 preferred third-octave centre frequencies, 20 Hz to 20 kHz.
    static const double *centres() noexcept;

    struct Params {
        float gainDb[kBands];
        // Scales the whole curve. Correction curves are often worth applying at
        // half strength, and halving thirty-one numbers by hand is not a thing
        // anyone should have to do.
        float amount;
    };

    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &p) noexcept;
    void process(const AudioBuffer &buf);

    double responseDb(double freqHz) const noexcept { return m_eq.responseDb(freqHz); }

private:
    Equalizer m_eq;
    double m_sampleRate = 48000.0;
};

// Solves for the band gains whose cascade matches `targetDb` at the band
// centres, and writes them to `gainsOut`.
//
// Necessary because the bands overlap: setting band i to the target at its own
// centre leaves every neighbour contributing there as well, and the realised
// curve comes out roughly a third too strong. A peaking section's shape is not
// quite proportional to its gain either -- RBJ's bandwidth widens as the gain
// grows -- so the correction is iterated rather than solved once.
//
// Not real-time; the interface runs it when a curve is imported or edited.
// Returns the largest remaining error in dB at the centres.
double fitGraphicEq(const double *targetDb, double sampleRate, float *gainsOut) noexcept;

} // namespace dreamdsp::dsp
