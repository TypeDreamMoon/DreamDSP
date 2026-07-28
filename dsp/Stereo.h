#pragma once

#include "BiquadFilter.h"

#include <vector>

namespace dreamdsp::dsp {

// Mid/side stereo width, with the low end optionally kept mono.
//
// Widening is a side-signal gain: M = (L+R)/2, S = (L-R)/2, scale S, sum back.
// Width 1 is exactly the identity, which matters -- an effect that colours the
// signal at its neutral setting is a trap.
//
// Bass below `monoBelowHz` is forced to the centre because wide low frequencies
// are the usual cause of a mix that collapses when summed to mono, and on
// speakers they simply blur.
class StereoWidener
{
public:
    struct Params {
        float width = 1.0f;         // 0 = mono, 1 = unchanged, 2 = doubled side
        float monoBelowHz = 0.0f;   // 0 disables
    };

    void prepare(double sampleRate);
    void reset();
    void setParams(const Params &p);

    void process(const AudioBuffer &buf);

private:
    Params m_p;
    double m_sampleRate = 48000.0;
    LinkwitzRiley4 m_splitS;   // splits the side signal
};

// ---------------------------------------------------------------------------

// Crossfeed, in the manner of bs2b.
//
// On speakers each ear hears both of them, slightly delayed and dulled by the
// head. Headphones deliver each channel to one ear only, which is why hard-
// panned material feels like it is inside your skull. Feeding a delayed,
// low-passed copy of each channel to the other approximates what the head
// would have done.
class Crossfeed
{
public:
    struct Params {
        float cutoffHz = 700.0f;   // above this the head shadows the far ear
        float feedDb = -4.5f;      // level of the crossed signal
        float delayUs = 300.0f;    // interaural time difference
    };

    void prepare(double sampleRate);
    void reset();
    void setParams(const Params &p);

    void process(const AudioBuffer &buf);

private:
    Params m_p;
    double m_sampleRate = 48000.0;

    BiquadFilter m_lpL, m_lpR;
    std::vector<float> m_delayL, m_delayR;
    int m_writeIndex = 0;
    int m_delaySamples = 0;
    float m_feed = 0.6f;
};

} // namespace dreamdsp::dsp
