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
    };

    void prepare(double sampleRate, int channels);
    void reset();
    void setParams(const Params &p);

    void process(const AudioBuffer &buf);

private:
    static constexpr int kMaxChannels = 8;

    Params m_p;
    double m_sampleRate = 48000.0;
    BiquadFilter m_hp1[kMaxChannels], m_hp2[kMaxChannels];
    Oversampler2x m_os[kMaxChannels];
    DcBlocker m_dc[kMaxChannels];
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
    int m_channels = 0;
};

} // namespace dreamdsp::dsp
