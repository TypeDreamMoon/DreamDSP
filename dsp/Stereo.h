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
        // Phantom-source widening, which is a different operation from `width`
        // and fixes what `width` provably cannot.
        //
        // A side gain only rescales a side signal that is already there. On
        // genuinely mono content S is zero, so the inter-channel correlation
        // stays at 1.0 at any gain -- the control does nothing. And on already-
        // wide material it drives the correlation negative (measured: an input
        // correlation of 0.7 becomes -0.23 at a gain of 3), which is not
        // "wider", it is unstable imaging.
        //
        // This instead applies Zotter and Frank's phase-based shuffler to the
        // mid signal. Of everything in this area it is the only method with a
        // published pairwise listening test behind it, and it is the cheapest:
        // about eighteen operations per sample for the pair, roughly two
        // biquads. The per-channel response is flat to 0.01 dB, the worst
        // mono-sum dip is bounded at 20*log10(cos v), and the resulting
        // inter-channel coherence has a closed form, J0(2v), so the control can
        // be labelled with what it actually does.
        //
        // Costs latency -- see latencySamples().
        float phaseAmount = 0.0f;   // 0 .. 1
    };

    void prepare(double sampleRate);
    void reset();
    void setParams(const Params &p);

    void process(const AudioBuffer &buf);

    // The phase widener's group delay, in samples, or zero when it is off.
    //
    // The published filter is symmetric in time -- it reaches 2N samples into
    // the future as well as the past -- so making it causal costs 2N samples.
    // At the 1 ms tap spacing used here that is 2 ms, independent of sample
    // rate. Turning the control on or off therefore changes the stream's
    // latency, and Windows asks an APO for that figure once when the stream is
    // built: this is a setting to choose before playback starts, not during.
    int latencySamples() const noexcept { return m_phaseActive ? 2 * m_tap : 0; }

private:
    static constexpr int kMaxTap = 256;   // 1 ms at 192 kHz is 192

    Params m_p;
    double m_sampleRate = 48000.0;
    LinkwitzRiley4 m_splitS;   // splits the side signal

    // Zotter-Frank. The delay line holds 4N+1 samples of mid, and one of side
    // so the untouched side path stays aligned with the widened mid.
    int m_tap = 0;
    bool m_phaseActive = false;
    float m_g0 = 1.0f, m_g1 = 0.0f, m_g2 = 0.0f;
    std::vector<float> m_midLine, m_sideLine;
    int m_lineLen = 0;
    int m_write = 0;
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
