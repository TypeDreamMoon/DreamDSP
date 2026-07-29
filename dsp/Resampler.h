#pragma once

#include <vector>

namespace dreamdsp::dsp {

// Band-limited sample-rate conversion for a finite signal.
//
// This exists so that an impulse response recorded at any rate works on any
// device. Equalizer APO's Convolution: command requires the file to be at
// exactly the endpoint's rate and is silently wrong otherwise, which is the
// single most confusing thing about using it -- the convolution appears to
// work, it just sounds subtly incorrect and shifted in pitch.
//
// Offline only. It allocates, runs over a whole signal at once, and is called
// when an impulse response is loaded -- never from an audio callback.
//
// The method is Kaiser-windowed sinc interpolation, which is the standard
// answer for arbitrary ratios: the ideal reconstruction filter is a sinc, and
// the Kaiser window is what makes a truncated one behave, with a single
// parameter trading stopband depth against transition width.
class Resampler
{
public:
    struct Quality {
        // Sinc zero crossings retained on each side. Cost is linear in this;
        // 32 puts the truncation error far below the window's stopband.
        int zeroCrossings = 32;
        // Kaiser beta. 10 gives roughly -100 dB stopband, which is well below
        // the noise floor of any real impulse response.
        double beta = 10.0;
    };

    // Resamples `in` from inRate to outRate. Returns an empty vector when the
    // arguments make no sense.
    //
    // Anti-aliasing is handled by lowering the sinc cutoff when downsampling,
    // so content above the new Nyquist is removed rather than folded back --
    // the failure that makes a naive resampler audibly wrong.
    static std::vector<float> resample(const float *in, int inFrames,
                                       double inRate, double outRate,
                                       Quality quality = {});

    // Number of output frames `resample` will produce.
    static int outputFrames(int inFrames, double inRate, double outRate);
};

} // namespace dreamdsp::dsp
