#pragma once

#include "BiquadFilter.h"
#include "Oversampler.h"

namespace dreamdsp::dsp {

// Tube-style saturation, and the exciter and virtual-bass stages that are
// built from the same waveshaper. Grouped because they share the oversampler
// and the harmonic generator; splitting them would mean three copies of the
// same argument about aliasing.

// Asymmetric soft clipper. The asymmetry is the whole point: a symmetric
// curve produces only odd harmonics, which sound like a fuzz pedal. Offsetting
// the operating point makes even harmonics -- the second in particular -- and
// that is what "tube warmth" refers to.
float tubeShape(float x, float drive, float bias);

// ---------------------------------------------------------------------------

class TubeStage
{
public:
    struct Params {
        float drive = 2.0f;     // 1 .. 20
        float bias = 0.2f;      // 0 = symmetric (odd only), 0.5 = strongly even
        float mix = 0.5f;       // dry/wet
        float outputDb = 0.0f;
    };

    void prepare(double sampleRate, int channels);
    void reset();
    void setParams(const Params &p) { m_p = p; }

    void process(const AudioBuffer &buf);

private:
    static constexpr int kMaxChannels = 8;

    Params m_p;
    Oversampler2x m_os[kMaxChannels];
    DcBlocker m_dc[kMaxChannels];
    int m_channels = 0;
};

// ---------------------------------------------------------------------------

// Aphex-style aural exciter: split off the top, distort only that, blend back.
// Distorting the whole signal muddies everything; distorting only the highs
// adds harmonics an octave above them, which reads as detail rather than dirt.
class Exciter
{
public:
    struct Params {
        float frequencyHz = 3500.0f;   // where the "high band" starts
        float drive = 3.0f;
        float amount = 0.3f;           // how much of the shaped band to add back
        // Below this the shaper does nothing at all.
        //
        // This is the part of Aphex's 1979 patent that everyone leaves out and
        // that is actually the idea. Their harmonic generator is a diode
        // clipper with an adjustable threshold, and the patent is explicit
        // about why: "by selecting the proper threshold only transient portions
        // of the signal become clipped". A static waveshaper on a high-passed
        // band adds a constant fizz; one that engages only above a threshold
        // adds air on transients and leaves sustained material alone. Set it to
        // -120 dB to get the static behaviour back.
        float thresholdDb = -45.0f;    // -120 .. -12
    };

    void prepare(double sampleRate, int channels);
    void reset();
    void setParams(const Params &p);

    void process(const AudioBuffer &buf);

private:
    static constexpr int kMaxChannels = 8;

    Params m_p;
    double m_sampleRate = 48000.0;
    // Designed at *four times* the sample rate: the band split happens inside
    // the oversampler pair, so that the dry and wet paths share one phase
    // response. See Exciter::process.
    BiquadFilter m_hp1[kMaxChannels], m_hp2[kMaxChannels];
    // Two 2x stages, nested. Of the three saturation stages this is the one
    // that genuinely needs 4x: measured against a 16x reference on 3-16 kHz
    // noise at realistic drive, the alias-to-signal ratio is 20 dB at 1x,
    // 47 dB at 2x and 89 dB at 4x. The aliases from a band that is already at
    // the top of the spectrum fold *downwards*, into the midrange, where
    // nothing masks them -- which is the measurable form of the complaint that
    // exciters sound like "treble with grit on it".
    Oversampler2x m_os[kMaxChannels];
    Oversampler2x m_os2[kMaxChannels];
    DcBlocker m_dc[kMaxChannels];
    float m_threshold = 0.0f;
    int m_channels = 0;
};

// ---------------------------------------------------------------------------

// Psychoacoustic bass, the trick behind MaxxBass and "ViPER bass".
//
// A small speaker cannot reproduce 40 Hz. But the ear infers a missing
// fundamental from its harmonic series -- given 80, 120 and 160 Hz it hears
// 40 Hz that is not physically present. So: take the content below what the
// speaker can manage, generate its harmonics, and play those instead.
//
// `cutoffHz` is the speaker's real low limit, which is why the control is
// usually labelled "speaker size" rather than a frequency.
class VirtualBass
{
public:
    struct Params {
        float cutoffHz = 90.0f;    // 40 .. 200
        float amount = 0.5f;       // level of the generated harmonics
        float drive = 4.0f;
        bool removeOriginal = false;   // high-pass away what the speaker cannot play
    };

    void prepare(double sampleRate, int channels);
    void reset();
    void setParams(const Params &p);

    void process(const AudioBuffer &buf);

private:
    static constexpr int kMaxChannels = 8;

    Params m_p;
    double m_sampleRate = 48000.0;

    // Split off the sub band, generate harmonics, band-limit them to the
    // octave or two above the cutoff so nothing lands back under it.
    //
    // The harmonic high-pass is 4th order, not 2nd: the waveshaper's output is
    // still dominated by the input frequency, and a gentle slope lets the very
    // fundamental this is supposed to replace leak straight back through.
    LinkwitzRiley4 m_split[kMaxChannels];
    BiquadFilter m_harmHp1[kMaxChannels], m_harmHp2[kMaxChannels];
    BiquadFilter m_harmLp[kMaxChannels];
    BiquadFilter m_outHp1[kMaxChannels], m_outHp2[kMaxChannels];
    Oversampler2x m_os[kMaxChannels];
    DcBlocker m_dc[kMaxChannels];

    // Envelope of the sub band, used to hold the shaper's operating point
    // still. A memoryless nonlinearity's harmonic *ratios* are a function of
    // how hard it is driven, so without this the effect's timbre and its
    // apparent strength both move with how loud the track was mastered:
    // measured on the arctangent/square-root shaper, the second harmonic
    // shifts 26.6 dB relative to the fundamental over 21 dB of input level.
    // Normalising in and back out afterwards makes the ratios programme-
    // independent, which is what the control on the panel implies they are.
    //
    // Root-mean-square, not a peak follower -- and unlike everywhere else in
    // this file, that is the right choice here.
    //
    // A peak follower on a 45 Hz note ripples at 90 Hz, and 90 Hz is inside the
    // band this stage synthesises. Feeding that ripple into the shaper and
    // dividing it back out afterwards does not cancel: the two multiplies sit
    // on opposite sides of a nonlinearity. Measured, it cost about 6% of the
    // second harmonic -- the normaliser modulating exactly the harmonics it
    // exists to stabilise. The mean square of a sine is a constant, so an RMS
    // detector has no such ripple to remove and needs no slow second stage to
    // hide it behind.
    float m_ms[kMaxChannels] = {};   // smoothed mean square of the sub band
    float m_msRate = 0.0f;
    int m_channels = 0;
};

} // namespace dreamdsp::dsp
