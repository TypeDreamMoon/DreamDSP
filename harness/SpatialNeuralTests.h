#pragma once

#include "Binaural.h"
#include "NeuralSaturation.h"
#include "models/SyntheticMemoryDriveReference.h"
#include "hrtf/KemarDiffuseData.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace dreamdsp::tests {

// Pass the harness's check(bool, string) callback. No dependency on its globals.
template <typename Check>
void runSpatialNeuralTests(Check &&check)
{
    using namespace dreamdsp::dsp;
    using Stereo = std::array<std::vector<float>, 2>;
    const auto blank = [](int frames) { return Stereo{ std::vector<float>(size_t(frames)),
                                                      std::vector<float>(size_t(frames)) }; };
    const auto process = [](auto &effect, Stereo &x, int offset, int frames) {
        float *channels[] = { x[0].data() + offset, x[1].data() + offset };
        effect.process({ channels, 2, frames });
    };
    const auto equal = [](const Stereo &a, const Stereo &b, float tolerance) {
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < a[size_t(ch)].size(); ++i)
                if (std::fabs(a[size_t(ch)][i] - b[size_t(ch)][i]) > tolerance) return false;
        return true;
    };
    const auto peakAt = [](const std::vector<float> &x) {
        return int(std::max_element(x.begin(), x.end(), [](float a, float b) {
            return std::fabs(a) < std::fabs(b);
        }) - x.begin());
    };
    const auto energy = [](const std::vector<float> &x) {
        double value = 0.0;
        for (float sample : x) value += double(sample) * sample;
        return value;
    };
    const auto finite = [](float x) {
        uint32_t bits;
        std::memcpy(&bits, &x, sizeof bits);
        return (bits & 0x7f800000u) != 0x7f800000u;
    };

    Binaural::Params spatial;
    spatial.model = 0; // Preserve the original analytic-model reference checks.
    spatial.speakerAngleDeg = 60.0f;
    spatial.outputDb = 0.0f;
    Binaural renderer;
    renderer.setParams(spatial);
    renderer.prepare(48000.0, 2);
    check(renderer.supported(), "binaural: 48 kHz stereo is supported");
    check(renderer.filterLength() >= 128 && renderer.filterLength() <= 256,
          "binaural: 48 kHz uses a bounded short analytic FIR");

    auto leftImpulse = blank(1024);
    leftImpulse[0][0] = 1.0f;
    process(renderer, leftImpulse, 0, 1024);
    renderer.reset();
    auto rightImpulse = blank(1024);
    rightImpulse[1][0] = 1.0f;
    process(renderer, rightImpulse, 0, 1024);
    check(energy(leftImpulse[0]) > 0.01 && energy(leftImpulse[1]) > 0.01
          && energy(rightImpulse[0]) > 0.01 && energy(rightImpulse[1]) > 0.01,
          "binaural: all four input-to-ear impulse paths are nonzero");
    check(peakAt(leftImpulse[0]) < peakAt(leftImpulse[1])
          && peakAt(rightImpulse[1]) < peakAt(rightImpulse[0]),
          "binaural: ipsilateral ear arrives before contralateral ear");
    check(energy(leftImpulse[0]) > energy(leftImpulse[1]) * 1.3,
          "binaural: head shadow changes energy as well as time of arrival");
    Stereo mirrored{ rightImpulse[1], rightImpulse[0] };
    check(equal(leftImpulse, mirrored, 2e-5f), "binaural: left/right geometry is symmetric");
    bool dcUnity = true;
    for (const auto &ch : leftImpulse) {
        double sum = 0.0;
        for (float sample : ch) sum += sample;
        dcUnity = dcUnity && std::fabs(sum - 1.0) < 1e-4;
    }
    check(dcUnity, "binaural: fractional delays and head filters retain unity DC per path");

    constexpr int n = 4096;
    auto signal = blank(n);
    for (int i = 0; i < n; ++i) {
        signal[0][size_t(i)] = 0.25f * std::sin(float(i) * 0.031f);
        signal[1][size_t(i)] = 0.19f * std::cos(float(i) * 0.057f);
    }
    auto whole = signal, chunks = signal;
    Binaural a, b;
    a.prepare(48000.0, 2); b.prepare(48000.0, 2);
    auto initialSilenceA = blank(32), initialSilenceB = blank(32);
    process(a, initialSilenceA, 0, 32); process(b, initialSilenceB, 0, 32);
    spatial.yawDeg = 170.0f;
    a.setParams(spatial); b.setParams(spatial);
    process(a, whole, 0, n);
    for (int offset = 0; offset < n;) {
        const int frames = std::min(1 + (offset * 29 + 31) % 173, n - offset);
        process(b, chunks, offset, frames);
        offset += frames;
    }
    check(equal(whole, chunks, 2e-6f), "binaural: direction smoothing and history are block-size invariant");

    spatial.mix = 0.0f;
    spatial.outputDb = 0.0f;
    a.setParams(spatial); a.reset();
    whole = signal;
    process(a, whole, 0, n);
    bool aligned = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) {
            const float expected = signal[size_t(ch)][size_t(i)];
            aligned = aligned && whole[size_t(ch)][size_t(i)] == expected;
        }
    check(aligned, "binaural: zero mix is bit-exact direct dry audio");
    check(a.latencySamples() == 0 && a.acousticDelaySamples() == 29,
          "binaural: zero block latency is distinct from the 48 kHz acoustic HRIR delay");
    a.prepare(48000.0, 2);
    a.setParams(spatial);
    whole = signal;
    process(a, whole, 0, n);
    check(equal(whole, signal, 0.0f), "binaural: initial parameters are applied before the first audio sample");
    Binaural highRate;
    highRate.prepare(192000.0, 2);
    check(highRate.supported() && highRate.filterLength() > a.filterLength(),
          "binaural: high sample rates extend the FIR instead of silently clipping the tail");
    Binaural mono;
    mono.prepare(48000.0, 1);
    whole = signal;
    process(mono, whole, 0, n);
    check(!mono.supported() && equal(whole, signal, 0.0f),
          "binaural: unsupported channel layouts pass through");
    Binaural invalidRate;
    invalidRate.prepare(std::numeric_limits<double>::quiet_NaN(), 2);
    check(!invalidRate.supported(), "binaural: invalid sample rates are rejected before allocating filters");
    spatial.speakerAngleDeg = std::numeric_limits<float>::quiet_NaN();
    spatial.yawDeg = std::numeric_limits<float>::infinity();
    spatial.mix = std::numeric_limits<float>::quiet_NaN();
    spatial.outputDb = std::numeric_limits<float>::infinity();
    a.setParams(spatial); a.reset();
    whole = signal;
    process(a, whole, 0, n);
    bool finiteSpatial = true;
    for (const auto &ch : whole)
        for (float sample : ch) finiteSpatial = finiteSpatial && finite(sample);
    check(finiteSpatial, "binaural: non-finite parameters cannot poison filter interpolation");

    Binaural measured;
    Binaural::Params mp;
    mp.model = 1; mp.outputDb = 0; mp.speakerAngleDeg = 90;
    measured.setParams(mp); measured.prepare(44100, 2);
    auto measuredLeft = blank(1024), measuredRight = blank(1024);
    measuredLeft[0][0] = 1.0f; process(measured, measuredLeft, 0, 1024);
    measured.reset(); measuredRight[1][0] = 1.0f; process(measured, measuredRight, 0, 1024);
    check(peakAt(measuredLeft[0]) < peakAt(measuredLeft[1])
          && peakAt(measuredRight[1]) < peakAt(measuredRight[0]),
          "KEMAR: documented clockwise/right azimuth maps to the correct ipsilateral ear");
    const int rawIndex = kemar_diffuse::kRings[4].first + 18; // elevation 0, clockwise 90.
    float pcmError = 0.0f;
    for (int ear = 0; ear < 2; ++ear)
        for (int i = 0; i < 128; ++i)
            pcmError = std::max(pcmError, std::fabs(
                measuredRight[size_t(ear)][size_t(i + measured.acousticDelaySamples())]
                - kemar_diffuse::kHrir[rawIndex][ear][i] / 32768.0f));
    check(pcmError < 1e-6f && measured.acousticDelaySamples() == 32,
          "KEMAR: native-rate FIR reproduces the official PCM samples and retained ITD");
    mirrored = {measuredRight[1], measuredRight[0]};
    check(equal(measuredLeft, mirrored, 2e-6f), "KEMAR: mirrored hemisphere swaps ears without changing samples");

    const auto response = [&](Binaural &effect, const Binaural::Params &params) {
        effect.setParams(params); effect.reset();
        auto impulse = blank(1024); impulse[0][0] = 1.0f;
        process(effect, impulse, 0, 1024);
        return impulse;
    };
    const auto magnitude = [](const std::vector<float> &ir, double hz, double rate) {
        double re = 0, im = 0;
        for (size_t i = 0; i < ir.size(); ++i) {
            const double phase = 6.283185307179586 * hz * double(i) / rate;
            re += ir[i] * std::cos(phase); im -= ir[i] * std::sin(phase);
        }
        return std::sqrt(re * re + im * im);
    };
    mp.speakerAngleDeg = 0; mp.elevationDeg = -30;
    const auto below = response(measured, mp);
    mp.elevationDeg = 60;
    const auto above = response(measured, mp);
    double spectralChange = 0;
    for (double hz : {4000.0, 8000.0, 12000.0})
        spectralChange += std::fabs(20 * std::log10((magnitude(above[0], hz, 44100) + 1e-9)
                                                    / (magnitude(below[0], hz, 44100) + 1e-9)));
    check(spectralChange > 3.0, "KEMAR: elevation changes measured spectral cues, not only gain or front/back position");
    mp.elevationDeg = 90; mp.yawDeg = 0;
    const auto overhead = response(measured, mp);
    mp.yawDeg = 117;
    const auto overheadRotated = response(measured, mp);
    check(equal(overhead, overheadRotated, 1e-6f), "KEMAR: zenith is independent of azimuth");
    mp.elevationDeg = 0; mp.speakerAngleDeg = 0; mp.yawDeg = 90;
    const auto clockwise = response(measured, mp);
    check(peakAt(clockwise[1]) < peakAt(clockwise[0]),
          "KEMAR: positive wire yaw rotates the speakers clockwise to listener's right");

    mp.speakerAngleDeg = 60; mp.yawDeg = 0; mp.elevationDeg = 0;
    const auto native = response(measured, mp);
    bool resampled = true;
    for (double rate : {32000.0, 48000.0, 96000.0, 192000.0}) {
        measured.prepare(rate, 2);
        const auto ir = response(measured, mp);
        resampled &= measured.supported() && peakAt(ir[0]) < peakAt(ir[1]);
        for (int ear = 0; ear < 2; ++ear)
            for (double hz : {1000.0, 4000.0, 8000.0}) {
                const double errorDb = 20 * std::log10((magnitude(ir[size_t(ear)], hz, rate) + 1e-9)
                                                     / (magnitude(native[size_t(ear)], hz, 44100) + 1e-9));
                resampled &= std::fabs(errorDb) < 0.15;
            }
    }
    check(resampled, "KEMAR: band-limited preparation preserves physical-rate response and ITD through 192 kHz");

    measured.prepare(44100, 2);
    mp.elevationDeg = 40; mp.speakerAngleDeg = 360.0f / 56.0f;
    const auto fractionalGrid = response(measured, mp);
    pcmError = 0.0f;
    const int fractionalIndex = kemar_diffuse::kRings[8].first + 1;
    for (int ear = 0; ear < 2; ++ear)
        for (int i = 0; i < 128; ++i)
            pcmError = std::max(pcmError, std::fabs(fractionalGrid[size_t(ear)][size_t(i + 32)]
                - kemar_diffuse::kHrir[fractionalIndex][1 - ear][i] / 32768.0f));
    check(pcmError < 2e-6f, "KEMAR: 56-point elevation ring uses physical 6.42857-degree steps, not rounded filenames");
    mp.speakerAngleDeg = 60; mp.elevationDeg = 10;
    const auto lowElevation = response(measured, mp);
    mp.elevationDeg = 20;
    const auto highElevation = response(measured, mp);
    mp.elevationDeg = 15;
    const auto halfwayElevation = response(measured, mp);
    float interpolationError = 0.0f;
    for (int ear = 0; ear < 2; ++ear)
        for (int i = 0; i < 1024; ++i)
            interpolationError = std::max(interpolationError,
                std::fabs(halfwayElevation[size_t(ear)][size_t(i)]
                    - 0.5f * (lowElevation[size_t(ear)][size_t(i)] + highElevation[size_t(ear)][size_t(i)])));
    check(interpolationError < 2e-6f, "KEMAR: intermediate elevation blends both measured rings");
    mp.elevationDeg = -40;
    const auto lowest = response(measured, mp);
    mp.elevationDeg = -90;
    const auto belowDataset = response(measured, mp);
    check(equal(lowest, belowDataset, 0.0f), "KEMAR: elevations below dataset coverage clamp to -40 degrees");

    // A model change must begin from the running response, not replace the FIR
    // instantly or clear history. Compare the first changed output with both
    // independently warmed model endpoints and the 20 ms crossfade weight.
    Binaural legacyEndpoint, measuredEndpoint, transition;
    mp.elevationDeg = 30; mp.model = 0;
    legacyEndpoint.setParams(mp); transition.setParams(mp);
    mp.model = 1; measuredEndpoint.setParams(mp);
    legacyEndpoint.prepare(48000, 2); measuredEndpoint.prepare(48000, 2); transition.prepare(48000, 2);
    auto legacyWarm = signal, measuredWarm = signal, transitionWarm = signal;
    process(legacyEndpoint, legacyWarm, 0, n); process(measuredEndpoint, measuredWarm, 0, n);
    process(transition, transitionWarm, 0, n);
    transition.setParams(mp);
    auto legacyNext = signal, measuredNext = signal, transitionNext = signal;
    process(legacyEndpoint, legacyNext, 0, 1); process(measuredEndpoint, measuredNext, 0, 1);
    process(transition, transitionNext, 0, 1);
    bool gradual = true;
    for (int ear = 0; ear < 2; ++ear) {
        const float oldValue = legacyNext[size_t(ear)][0], newValue = measuredNext[size_t(ear)][0];
        const float expected = oldValue + (newValue - oldValue) / 960.0f;
        gradual &= std::fabs(transitionNext[size_t(ear)][0] - expected) < 2e-6f;
    }
    check(gradual, "KEMAR: model switch starts with a 20 ms crossfade using retained common input history");

    Binaural movingA, movingB;
    mp.model = 0; mp.elevationDeg = 0;
    movingA.setParams(mp); movingB.setParams(mp);
    movingA.prepare(48000, 2); movingB.prepare(48000, 2);
    auto warmA = signal, warmB = signal;
    process(movingA, warmA, 0, n); process(movingB, warmB, 0, n);
    mp.model = 1; mp.elevationDeg = 47; mp.yawDeg = -170;
    movingA.setParams(mp); movingB.setParams(mp);
    whole = signal; chunks = signal;
    process(movingA, whole, 0, n);
    for (int offset = 0; offset < n;) {
        const int frames = std::min(1 + (offset * 17 + 3) % 113, n - offset);
        process(movingB, chunks, offset, frames); offset += frames;
    }
    check(equal(whole, chunks, 2e-6f), "KEMAR: elevation, ring rotation and model crossfade are block-size invariant");
    bool finiteSwitch = true;
    for (const auto &ch : whole) for (float value : ch) finiteSwitch &= finite(value) && std::fabs(value) <= 4;
    check(finiteSwitch, "KEMAR: live analytic-to-measured switch remains finite without clearing input history");
    mp.elevationDeg = std::numeric_limits<float>::quiet_NaN();
    mp.model = std::numeric_limits<uint32_t>::max();
    const auto badMeasured = response(movingA, mp);
    bool safeMeasured = true;
    for (const auto &ch : badMeasured) for (float value : ch) safeMeasured &= finite(value);
    check(safeMeasured, "KEMAR: invalid elevation/model cannot produce an invalid table address");

    // Repeated switches and moving angles can also be wrapped by the parent
    // harness's allocation counter: all model resources are already prepared.

    NeuralSaturation neural;
    neural.prepare(48000.0, 2);
    auto reference = blank(64);
    for (int i = 0; i < 64; ++i)
        reference[0][size_t(i)] = reference[1][size_t(i)] =
            synthetic_memory_drive_reference::kInput[i];
    process(neural, reference, 0, 64);
    float oracleError = 0.0f;
    for (int i = 0; i < 64; ++i)
        oracleError = std::max(oracleError, std::fabs(reference[0][size_t(i)]
                            - synthetic_memory_drive_reference::kOutput[i]));
    check(oracleError < 2e-5f, "neural: C++ inference agrees with the independent Python oracle");
    check(reference[0] == reference[1], "neural: identical stereo input has identical output");
    NeuralSaturation na, nb;
    na.prepare(48000.0, 2); nb.prepare(48000.0, 2);
    initialSilenceA = blank(32); initialSilenceB = blank(32);
    process(na, initialSilenceA, 0, 32); process(nb, initialSilenceB, 0, 32);
    NeuralSaturation::Params np;
    np.driveDb = 9.0f;
    np.mix = 0.72f;
    na.setParams(np); nb.setParams(np);
    whole = signal; chunks = signal;
    process(na, whole, 0, n);
    for (int offset = 0; offset < n;) {
        const int frames = std::min(1 + (offset * 13 + 7) % 197, n - offset);
        process(nb, chunks, offset, frames);
        offset += frames;
    }
    check(equal(whole, chunks, 2e-6f), "neural: state and shared smoothing are block-size invariant");

    neural.reset();
    auto isolated = blank(1024);
    isolated[0][0] = 1.0f;
    process(neural, isolated, 0, 1024);
    double memoryEnergy = 0.0;
    for (int i = 1; i < 64; ++i) memoryEnergy += double(isolated[0][size_t(i)]) * isolated[0][size_t(i)];
    check(memoryEnergy > 1e-5, "neural: an impulse produces a stateful response after the input ends");
    check(energy(isolated[1]) == 0.0, "neural: no recurrent state leaks into the silent channel");

    bool bypass = true;
    for (double rate : { 0.0, 4000.0, 384000.0 }) {
        neural.prepare(rate, 2);
        whole = signal;
        process(neural, whole, 0, n);
        bypass = bypass && !neural.supported() && equal(whole, signal, 0.0f);
    }
    check(bypass, "neural: unsupported rates are bit-exact passthrough");
    neural.prepare(48000.0, 2);
    np.mix = 0.0f;
    np.outputDb = 0.0f;
    neural.setParams(np);
    whole = signal;
    process(neural, whole, 0, n);
    check(equal(whole, signal, 0.0f), "neural: zero mix and unity output are bit-exact");
    np = {};
    neural.setParams(np);
    neural.prepare(48000.0, 2);
    auto dc = blank(48000);
    std::fill(dc[0].begin(), dc[0].end(), 0.2f);
    process(neural, dc, 0, 48000);
    check(std::fabs(dc[0].back()) < 1e-4f, "neural: generated/output DC decays without resetting between blocks");

    neural.reset();
    auto poisoned = blank(256);
    poisoned[0][0] = std::numeric_limits<float>::quiet_NaN();
    poisoned[1][0] = std::numeric_limits<float>::infinity();
    poisoned[0][1] = poisoned[1][1] = 0.3f;
    process(neural, poisoned, 0, 256);
    bool recovered = neural.faultCount() == 2;
    for (const auto &ch : poisoned)
        for (float sample : ch) recovered = recovered && finite(sample);
    check(recovered && std::fabs(poisoned[0][1]) > 0.01f,
          "neural: invalid input resets only the affected channel and recovers immediately");
}

// Optional measured offline timing, separate from correctness assertions.
// This ordinary process benchmark is NOT a WCET or an APO glitch guarantee.
inline void reportSpatialNeuralPerformance()
{
    using namespace dreamdsp::dsp;
    const auto measure = [](const char *name, auto &effect, auto update,
                            double rate, int frames) {
        constexpr int blocks = 2048;
        std::array<float, 256> left{}, right{};
        std::array<double, blocks> elapsed{};
        float *channels[] = { left.data(), right.data() };
        double total = 0.0;
        int missed = 0;
        const double deadline = 1000000.0 * frames / rate;
        for (int block = -32; block < blocks; ++block) {
            for (int i = 0; i < frames; ++i) {
                left[size_t(i)] = 0.2f * float((i + block + 32) % 47) / 47.0f;
                right[size_t(i)] = 0.17f * float((i * 3 + block + 32) % 53) / 53.0f;
            }
            const auto start = std::chrono::steady_clock::now();
            update(block + 32);
            effect.process({ channels, 2, frames });
            const auto stop = std::chrono::steady_clock::now();
            if (block < 0) continue;
            const double us = std::chrono::duration<double, std::micro>(stop - start).count();
            elapsed[size_t(block)] = us;
            total += us;
            if (us > deadline) ++missed;
        }
        std::sort(elapsed.begin(), elapsed.end());
        std::printf("  %s %.0f Hz/%d: mean %.2f us, p99 %.2f us, max %.2f us; "
                    "max %.2f%% of %.2f us period, %d/%d misses\n",
                    name, rate, frames, total / blocks, elapsed[size_t(blocks * 99 / 100)],
                    elapsed.back(), 100.0 * elapsed.back() / deadline, deadline, missed, blocks);
    };
    std::printf("\n[spatial/neural: offline timing; not measured in audiodg]\n");
    for (double rate : { 48000.0, 96000.0, 192000.0 }) {
        Binaural spatial;
        spatial.prepare(rate, 2);
        std::printf("  measured MIT KEMAR HRTF: %d taps, resampling causal offset %d samples\n",
                    spatial.filterLength(), spatial.acousticDelaySamples());
        for (int frames : { 64, 128, 256 }) {
            spatial.reset();
            measure("KEMAR", spatial, [&](int block) {
                Binaural::Params p;
                p.yawDeg = float(block % 360) - 180.0f;
                p.elevationDeg = float(block % 131) - 40.0f;
                spatial.setParams(p);
            }, rate, frames);
        }
    }
    Binaural switching;
    switching.prepare(48000.0, 2);
    measure("KEMAR/model-switch", switching, [&](int block) {
        Binaural::Params p;
        p.yawDeg = float(block % 360) - 180.0f;
        p.elevationDeg = float(block % 131) - 40.0f;
        p.model = uint32_t((block / 17) & 1);
        switching.setParams(p);
    }, 48000.0, 64);
    NeuralSaturation neural;
    neural.prepare(48000.0, 2);
    for (int frames : { 64, 128, 256 }) {
        neural.reset();
        measure("Neural", neural, [&](int block) {
            NeuralSaturation::Params p;
            p.driveDb = float(block % 19);
            neural.setParams(p);
        }, 48000.0, frames);
    }
}

} // namespace dreamdsp::tests
