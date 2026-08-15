#pragma once

#include "BiquadFilter.h"
#include "DspTypes.h"

namespace dreamdsp::dsp {

// Dynamic bass boost: JamesDSP's, and the counterpart to VirtualBass.
//
// The two solve opposite problems and are not substitutes. VirtualBass
// synthesises harmonics of a fundamental the transducer cannot reproduce, which
// is what a small speaker or an in-ear needs. This one lifts low end that is
// already there, which is what a speaker that can play it needs -- and lifts it
// by however much is left before full scale rather than by a fixed amount.
//
// What it guarantees, stated exactly, because the obvious stronger claim is
// false and worth saying so: the *boosted low band* never exceeds full scale.
// The stage's output is that band summed with the one above it, so the total
// can still land slightly over -- about half a decibel on a 50 Hz tone at full
// scale, which is the high band of the crossover adding in. Keeping the output
// itself under a ceiling is the limiter's job, and it does have an exact
// guarantee.
//
// Two things are needed even for the narrower claim, and only the first is
// obvious. The envelope is measured on the input, before the boost, so the gain
// is exactly min(maxGain, headroom) and the loop cannot chase itself.
//
// And the envelope attacks instantly. A smoothed attack was the first thing
// tried and it is quietly wrong: on a 50 Hz sine with a 10 ms attack the
// envelope settles well below the actual peak, the headroom it reports is
// therefore too large, and the boost overshot by more than two decibels -- a
// protective measurement must never lag the thing it is protecting against.
// There is consequently no attack control: it would only have degrees of
// wrongness to offer. Release is what shapes how it feels.
class DynamicBass
{
public:
    static constexpr int kMaxChannels = 8;

    struct Params {
        float maxGainDb = 6.0f;     // the most it will ever add
        float cutoffHz = 100.0f;    // top of the band it works on
        float releaseMs = 250.0f;   // how fast the boost comes back
        float reserved = 0.0f;
    };

    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &p) noexcept;
    void process(const AudioBuffer &buf);

    // What it is adding right now, for a meter.
    double appliedDb() const noexcept { return m_appliedDb; }

private:
    Params m_p;
    double m_sampleRate = 48000.0;
    int m_channels = 0;

    LinkwitzRiley4 m_split[kMaxChannels];
    float m_env = 0.0f;             // shared, so the channels stay in step
    float m_releaseCoef = 0.0f;
    float m_maxLin = 2.0f;
    double m_appliedDb = 0.0;
};

} // namespace dreamdsp::dsp
