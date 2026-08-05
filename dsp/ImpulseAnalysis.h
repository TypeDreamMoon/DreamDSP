#pragma once

#include <vector>

namespace dreamdsp::dsp {

// What an impulse response actually does to the spectrum.
//
// A directory of several hundred impulse responses is unusable without this.
// The file names say "Jazz Club" or "Apple iPod 01.Acoustic"; they do not say
// which one is a gentle tilt and which one carves 12 dB out of the presence
// region. One look at the curve settles it.
//
// Offline: allocates, runs over the whole response. Host-neutral so the maths
// can be checked against filters with a closed-form response.
struct ImpulseCurve {
    std::vector<float> magnitudeDb;   // one entry per point, low to high
    float minFreqHz = 20.0f;
    float maxFreqHz = 20000.0f;
    float peakDb = 0.0f;              // before normalisation, for reporting

    // Where the bottom of a plot should sit for this particular response.
    //
    // A fixed range makes a gentle tilt look like a flat line and wastes three
    // quarters of the height on nothing. Taken from a low percentile rather
    // than the minimum, so one deep null -- or the cliff at the top of an MP3
    // simulation -- does not flatten everything else against the ceiling.
    float suggestedFloorDb = -30.0f;
    bool valid() const { return !magnitudeDb.empty(); }
};

// Magnitude response of `taps` at `points` logarithmically spaced frequencies.
//
// Logarithmic because that is how the ear divides the spectrum: a linear axis
// spends three quarters of its width above 5 kHz and compresses everything
// below 1 kHz into a sliver, which is where most of what an impulse response
// does actually lives.
//
// The curve is normalised so its loudest point sits at 0 dB. What matters when
// choosing between responses is shape, not level -- level is handled separately
// and automatically.
ImpulseCurve impulseMagnitudeResponse(const float *taps, int tapCount,
                                      double sampleRate, int points = 256,
                                      double minFreqHz = 20.0,
                                      double maxFreqHz = 20000.0);

// Averaged over the channels of a planar multi-channel response, which is what
// a stereo impulse response needs: two curves drawn on top of each other are
// harder to read than one, and the channels of a usable response differ only in
// detail.
// Named differently rather than overloaded: the two differ only by an int in
// the middle, and every call site would resolve through implicit conversions
// that the compiler cannot choose between.
ImpulseCurve impulseMagnitudeResponsePlanar(const float *planar, int tapCount,
                                            int channels, double sampleRate,
                                            int points = 256);

} // namespace dreamdsp::dsp
