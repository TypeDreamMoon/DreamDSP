#pragma once
#include "NeuralSaturation.h"
#include "Resampler.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace dreamdsp::tests {
template<class Check>
void runNeuralRateTests(Check check)
{
    using namespace dsp;
    constexpr double pi = 3.14159265358979323846;
    const auto signal = [pi](double t) {
        return float(.25 * std::sin(2 * pi * 997 * t) + .12 * std::sin(2 * pi * 3011 * t));
    };
    const auto process = [](NeuralSaturation &stage, std::vector<float> &x, int chunk) {
        for (int at = 0; at < int(x.size()); at += chunk) {
            float *channels[]{x.data() + at};
            stage.process({channels, 1, std::min(chunk, int(x.size()) - at)});
        }
    };
    std::printf("\n[neural: sample-rate adaptation]\n");
    for (double rate : {8000., 16000., 22050., 32000., 44100., 48000., 88200., 96000., 176400., 192000.}) {
        NeuralSaturation a, b;
        a.prepare(rate, 1); b.prepare(rate, 1);
        NeuralSaturation::Params p;
        p.mix = .37f; p.driveDb = 7;
        a.setParams(p); b.setParams(p);
        std::vector<float> whole(4096), pieces;
        for (int i = 0; i < int(whole.size()); ++i) whole[size_t(i)] = signal(i / rate);
        const auto dry = whole;
        pieces = whole;
        process(a, whole, int(whole.size())); process(b, pieces, 17);
        float error = 0; bool finite = true;
        for (size_t i = 0; i < whole.size(); ++i) {
            error = std::max(error, std::abs(whole[i] - pieces[i]));
            finite &= std::isfinite(whole[i]) && std::abs(whole[i]) <= 4;
        }
        std::printf("  rate %.0f Hz, latency %d samples (%.3f ms)\n", rate, a.latencySamples(), 1000 * a.latencySamples() / rate);
        check(a.supported() && finite && error < 1e-7f,
              "neural SRC: host rates and arbitrary block boundaries preserve continuous finite state");
        p.mix = 0; p.outputDb = 0;
        a.setParams(p); a.reset(); whole = dry;
        process(a, whole, 1);
        bool aligned = true;
        const int delay = a.latencySamples();
        for (int i = 0; i < int(whole.size()); ++i)
            aligned &= whole[size_t(i)] == (i < delay ? 0.0f : dry[size_t(i - delay)]);
        check(aligned, "neural SRC: dry mix is bit-exact after the reported integer alignment delay");
    }

    // Independent, non-causal offline resampler supplies the cross-rate oracle.
    std::vector<float> reference(14400);
    for (int i = 0; i < int(reference.size()); ++i) reference[size_t(i)] = signal(i / 48000.0);
    NeuralSaturation native;
    native.prepare(48000, 1);
    process(native, reference, 64);
    for (double rate : {44100., 88200., 96000., 192000.}) {
        NeuralSaturation stage;
        stage.prepare(rate, 1);
        const auto expected = Resampler::resample(reference.data(), int(reference.size()), 48000, rate);
        std::vector<float> actual(expected.size());
        for (int i = 0; i < int(actual.size()); ++i) actual[size_t(i)] = signal(i / rate);
        process(stage, actual, 113);
        double error = 0, power = 0;
        for (int i = int(rate * .1); i < int(rate * .25); ++i) {
            const double y = actual[size_t(i + stage.latencySamples())], x = expected[size_t(i)];
            error += (y - x) * (y - x); power += x * x;
        }
        const double esr = 10 * std::log10((error + 1e-30) / (power + 1e-30));
        std::printf("  cross-rate %.0f Hz: %.2f dB error/signal\n", rate, esr);
        check(esr < -40, "neural SRC: same band-limited programme matches the 48 kHz model after alignment");
    }
    NeuralSaturation stage;
    stage.prepare(96000, 1);
    std::vector<float> ultrasonic(8192);
    for (int i = 0; i < int(ultrasonic.size()); ++i)
        ultrasonic[size_t(i)] = float(.5 * std::sin(2 * pi * 30000 * i / 96000));
    process(stage, ultrasonic, 64);
    float peak = 0;
    for (size_t i = 2048; i < ultrasonic.size(); ++i) peak = std::max(peak, std::abs(ultrasonic[i]));
    std::printf("  30 kHz rejection at 96 kHz: output peak %.8f\n", peak);
    check(peak < 1e-4f, "neural SRC: ultrasonic input is filtered before the 48 kHz model, not folded into audibility");

    stage.prepare(44100, 2);
    std::vector<float> left(4096, .1f), right(4096, 0);
    left[100] = std::numeric_limits<float>::quiet_NaN();
    left[500] = std::numeric_limits<float>::infinity();
    float *ch[]{left.data(), right.data()};
    stage.process({ch, 2, int(left.size())});
    bool safe = stage.faultCount() == 2;
    for (size_t i = 0; i < left.size(); ++i) safe &= std::isfinite(left[i]) && right[i] == 0;
    check(safe, "neural SRC: non-finite inputs recover without contaminating the other channel");
}
} // namespace dreamdsp::tests
