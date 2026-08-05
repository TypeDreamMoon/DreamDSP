#include "ImpulseAnalysis.h"

#include "Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace dreamdsp::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

int nextPowerOfTwo(int v)
{
    int n = 1;
    while (n < v)
        n <<= 1;
    return n;
}

} // namespace

ImpulseCurve impulseMagnitudeResponse(const float *taps, int tapCount,
                                      double sampleRate, int points,
                                      double minFreqHz, double maxFreqHz)
{
    ImpulseCurve curve;
    if (!taps || tapCount < 1 || !(sampleRate > 0.0) || points < 2)
        return curve;

    curve.minFreqHz = float(minFreqHz);
    curve.maxFreqHz = float(std::min(maxFreqHz, sampleRate * 0.5));

    // Zero-padded to at least twice the response, and to at least 8192 points
    // so the bins are close enough together that the bass end is not a handful
    // of samples wide. At 44.1 kHz, 8192 bins puts one every 5.4 Hz.
    const int fftLength = std::max(8192, nextPowerOfTwo(tapCount * 2));
    Fft fft;
    fft.prepare(fftLength / 2);
    if (!fft.valid())
        return curve;

    std::vector<float> padded(size_t(fftLength), 0.0f);
    const int copy = std::min(tapCount, fftLength);
    std::copy(taps, taps + copy, padded.begin());

    std::vector<std::complex<float>> bins(size_t(fft.realBins()),
                                          std::complex<float>(0.0f, 0.0f));
    fft.realForward(padded.data(), bins.data());

    const double binHz = sampleRate / double(fftLength);
    const int lastBin = fft.realBins() - 1;

    curve.magnitudeDb.assign(size_t(points), 0.0f);
    double peak = 0.0;

    for (int i = 0; i < points; ++i) {
        const double t = double(i) / double(points - 1);
        const double hz = curve.minFreqHz
                          * std::pow(double(curve.maxFreqHz) / curve.minFreqHz, t);

        // Averaged over the bins this point covers rather than sampled at one
        // of them. At the top of a log axis a single point spans dozens of
        // bins, and picking one turns a smooth response into noise.
        const double loHz = (i == 0) ? hz : hz / std::pow(double(curve.maxFreqHz)
                                                          / curve.minFreqHz,
                                                          0.5 / double(points - 1));
        const double hiHz = (i == points - 1)
                                ? hz
                                : hz * std::pow(double(curve.maxFreqHz) / curve.minFreqHz,
                                                0.5 / double(points - 1));

        int lo = int(std::floor(loHz / binHz));
        int hi = int(std::ceil(hiHz / binHz));
        lo = std::clamp(lo, 0, lastBin);
        hi = std::clamp(hi, lo, lastBin);

        double sum = 0.0;
        for (int b = lo; b <= hi; ++b)
            sum += double(std::abs(bins[size_t(b)]));
        const double magnitude = sum / double(hi - lo + 1);

        curve.magnitudeDb[size_t(i)] = float(magnitude);
        peak = std::max(peak, magnitude);
    }

    curve.peakDb = float(20.0 * std::log10(std::max(peak, 1e-12)));

    // Converted to dB and referred to the peak, so what is on screen is shape
    // rather than level. Floored rather than allowed to run to negative
    // infinity, which would otherwise dominate the vertical scale wherever a
    // response has a genuine null.
    for (float &v : curve.magnitudeDb) {
        const double rel = double(v) / std::max(peak, 1e-12);
        v = float(std::max(20.0 * std::log10(std::max(rel, 1e-6)), -60.0));
    }

    {
        std::vector<float> sorted = curve.magnitudeDb;
        std::sort(sorted.begin(), sorted.end());
        const size_t at = sorted.size() / 20;            // 5th percentile
        const float low = sorted[at];
        // At least a 12 dB window, so a nearly flat response still fills the
        // plot; at most 60, which is where the analysis floor is anyway.
        curve.suggestedFloorDb = std::clamp(low - 6.0f, -60.0f, -12.0f);
    }

    return curve;
}

ImpulseCurve impulseMagnitudeResponsePlanar(const float *planar, int tapCount,
                                            int channels, double sampleRate, int points)
{
    if (!planar || tapCount < 1 || channels < 1)
        return {};
    if (channels == 1)
        return impulseMagnitudeResponse(planar, tapCount, sampleRate, points);

    // Averaged in the magnitude domain, one channel at a time. Summing the
    // responses first and transforming once would let two decorrelated channels
    // cancel, drawing notches that neither of them has.
    ImpulseCurve total;
    for (int c = 0; c < channels; ++c) {
        const ImpulseCurve one = impulseMagnitudeResponse(
            planar + size_t(c) * tapCount, tapCount, sampleRate, points);
        if (!one.valid())
            return {};
        if (!total.valid()) {
            total = one;
        } else {
            for (size_t i = 0; i < total.magnitudeDb.size(); ++i)
                total.magnitudeDb[i] += one.magnitudeDb[i];
            total.peakDb = std::max(total.peakDb, one.peakDb);
            total.suggestedFloorDb = std::min(total.suggestedFloorDb, one.suggestedFloorDb);
        }
    }
    for (float &v : total.magnitudeDb)
        v /= float(channels);
    return total;
}

} // namespace dreamdsp::dsp
