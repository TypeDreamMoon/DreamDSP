#include "Resampler.h"

#include <cmath>

namespace dreamdsp::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Modified Bessel function of the first kind, order zero, by its series. The
// argument here never exceeds beta (about 10), where the series converges in a
// couple of dozen terms.
double besselI0(double x)
{
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 40; ++k) {
        term *= (x * 0.5) / double(k);
        const double contribution = term * term;
        sum += contribution;
        if (contribution < 1e-18 * sum)
            break;
    }
    return sum;
}

double sinc(double x)
{
    if (std::fabs(x) < 1e-12)
        return 1.0;
    const double px = kPi * x;
    return std::sin(px) / px;
}

} // namespace

int Resampler::outputFrames(int inFrames, double inRate, double outRate)
{
    if (inFrames <= 0 || !(inRate > 0.0) || !(outRate > 0.0))
        return 0;
    const double ratio = outRate / inRate;
    const int n = int(std::llround(double(inFrames) * ratio));
    return n > 0 ? n : 0;
}

std::vector<float> Resampler::resample(const float *in, int inFrames,
                                       double inRate, double outRate,
                                       Quality quality)
{
    std::vector<float> out;
    if (!in || inFrames <= 0 || !(inRate > 0.0) || !(outRate > 0.0))
        return out;

    const double ratio = outRate / inRate;

    // Nothing to do, and worth short-circuiting: the overwhelmingly common case
    // is an impulse response that already matches the device.
    if (std::fabs(ratio - 1.0) < 1e-12) {
        out.assign(in, in + inFrames);
        return out;
    }

    const int zeroCrossings = quality.zeroCrossings < 4 ? 4 : quality.zeroCrossings;
    const double beta = quality.beta < 0.0 ? 0.0 : quality.beta;

    // Cutoff in cycles per input sample. When downsampling, the filter has to
    // come down at the NEW Nyquist, not the old one -- otherwise everything
    // between the two folds back as aliasing, which is what makes a naive
    // resampler sound wrong rather than merely soft.
    const double cutoff = (ratio < 1.0) ? 0.5 * ratio : 0.5;

    // Half-width in input samples. Scaled by 1/(2*cutoff) so the kernel always
    // spans `zeroCrossings` zero crossings of the sinc, however wide the sinc
    // has been stretched by a low cutoff.
    const double halfWidth = double(zeroCrossings) / (2.0 * cutoff);
    const double invI0Beta = 1.0 / besselI0(beta);

    const int outFrames = outputFrames(inFrames, inRate, outRate);
    out.assign(size_t(outFrames), 0.0f);

    for (int n = 0; n < outFrames; ++n) {
        // Where this output sample sits, measured in input samples.
        const double centre = double(n) / ratio;

        const long long first = (long long)std::ceil(centre - halfWidth);
        const long long last = (long long)std::floor(centre + halfWidth);

        double acc = 0.0;
        double norm = 0.0;

        for (long long i = first; i <= last; ++i) {
            const double t = centre - double(i);
            const double w = std::fabs(t) / halfWidth;
            if (w > 1.0)
                continue;

            const double kaiser = besselI0(beta * std::sqrt(1.0 - w * w)) * invI0Beta;
            const double h = 2.0 * cutoff * sinc(2.0 * cutoff * t) * kaiser;

            // norm accumulates every tap in the window, including those whose
            // input sample does not exist. Normalising by only the taps that
            // landed inside the signal would assume the signal continues past
            // its ends at the same level -- wrong for an impulse response,
            // which starts abruptly. This way the ends decay naturally.
            norm += h;

            if (i >= 0 && i < inFrames)
                acc += h * double(in[size_t(i)]);
        }

        out[size_t(n)] = (norm > 1e-12) ? float(acc / norm) : 0.0f;
    }

    return out;
}

} // namespace dreamdsp::dsp
