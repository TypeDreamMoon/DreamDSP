#pragma once

#include "NoiseGate.h"
#include "DynamicEq.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace dreamdsp::tests {
namespace gateDynamicEqDetail {
using Stereo = std::array<std::vector<float>, 2>;
constexpr double pi = 3.14159265358979323846;
inline Stereo blank(int count) { return {std::vector<float>(size_t(count)), std::vector<float>(size_t(count))}; }
template <class Effect>
void run(Effect &effect, Stereo &audio, int start, int count)
{
    float *channels[]{audio[0].data() + start, audio[1].data() + start};
    effect.process({channels, 2, count});
}
inline bool finite(float value)
{
    uint32_t bits; std::memcpy(&bits, &value, sizeof bits);
    return (bits & 0x7F800000u) != 0x7F800000u;
}
inline double amplitude(const std::vector<float> &audio, double frequency, int start, int count)
{
    double real = 0, imag = 0;
    for (int i = start; i < start + count; ++i) {
        const double phase = 2 * pi * frequency * i / 48000.0;
        real += audio[size_t(i)] * std::cos(phase);
        imag -= audio[size_t(i)] * std::sin(phase);
    }
    return 2.0 * std::sqrt(real * real + imag * imag) / count;
}
inline Stereo constant(float value, int count, float rightScale = 1.0f)
{
    auto audio = blank(count);
    std::fill(audio[0].begin(), audio[0].end(), value);
    std::fill(audio[1].begin(), audio[1].end(), value * rightScale);
    return audio;
}
} // namespace gateDynamicEqDetail

template <class Check>
void runGateDynamicEqTests(Check &&check)
{
    using namespace dsp;
    using namespace gateDynamicEqDetail;
    std::printf("\n[linked noise gate]\n");
    NoiseGate gate;
    gate.prepare(48000, 2);
    NoiseGate::Params gp;
    gp.thresholdDb = -20; gp.hysteresisDb = 6; gp.rangeDb = 40;
    gp.attackMs = 5; gp.holdMs = 40; gp.releaseMs = 20;
    gate.setParams(gp); gate.reset();
    auto quiet = constant(0.01f, 1024);
    run(gate, quiet, 0, 1024);
    check(std::fabs(quiet[0].back() - 0.0001f) < 1e-8f && std::fabs(gate.reductionDb() - 40) < 1e-3f,
          "gate: reset starts closed at the requested attenuation floor");

    gate.reset();
    auto attack = constant(0.2f, 4800, 0.01f);
    run(gate, attack, 0, 4800);
    const double attackGain = attack[0][239] / 0.2;
    const double expectedAttack = 0.01 + 0.99 * (1 - std::exp(-1.0));
    float imageError = 0;
    for (int i = 0; i < 4800; ++i)
        imageError = std::max(imageError, std::fabs(attack[1][size_t(i)] - attack[0][size_t(i)] * 0.01f));
    check(std::fabs(attackGain - expectedAttack) < 2e-4 && attack[0].back() > 0.199f,
          "gate: opening attack is a 5 ms gain time constant without lookahead");
    check(imageError < 2e-8f && attack[1].back() > 0.00199f,
          "gate: a loud channel opens the same gain for a quiet channel");

    auto middle = constant(0.075f, 12000);
    run(gate, middle, 0, 12000);
    const float retainedOpen = middle[0].back() / 0.075f;
    gate.reset(); middle = constant(0.075f, 12000); run(gate, middle, 0, 12000);
    check(retainedOpen > 0.999f && middle[0].back() < 0.000751f,
          "gate: hysteresis region retains the previous open or closed state");

    gate.reset(); auto lead = constant(0.2f, 4800); run(gate, lead, 0, 4800);
    const float openGain = lead[0].back() / 0.2f;
    auto close = constant(0.001f, 6000); run(gate, close, 0, 6000);
    int firstClosing = -1;
    for (int i = 0; i < 6000; ++i)
        if (close[0][size_t(i)] / 0.001f < openGain - 1e-4f) { firstClosing = i; break; }
    const double expectedClosing = std::log(0.2 / dbToLin(-26.0f)) * 480.0 + 1920.0;
    const double releaseGain = firstClosing >= 0 ? close[0][size_t(firstClosing + 959)] / 0.001 : 0;
    const double expectedRelease = 0.01 + (openGain - 0.01) * std::exp(-1.0);
    std::printf("  hold: first closing sample %d (detector + hold ~%.0f); release gain %.5f\n",
                firstClosing, expectedClosing, releaseGain);
    check(firstClosing >= 0 && std::fabs(firstClosing - expectedClosing) < 4,
          "gate: 40 ms hold starts after the 10 ms peak detector crosses the close threshold");
    check(std::fabs(releaseGain - expectedRelease) < 3e-4,
          "gate: closing release is a 20 ms gain time constant");

    gate.reset(); lead = constant(0.2f, 4800); run(gate, lead, 0, 4800);
    auto burst = constant(0.001f, 1500); run(gate, burst, 0, 1500);
    auto interruption = constant(0.075f, 100); run(gate, interruption, 0, 100);
    burst = constant(0.001f, 2000); run(gate, burst, 0, 2000);
    check(burst[0].back() > 0.000999f,
          "gate: returning above the closing threshold restarts continuous hold timing");

    gate.reset(); quiet = constant(0.001f, 64); run(gate, quiet, 0, 64);
    gp.rangeDb = 0; gate.setParams(gp);
    auto fade = constant(0.001f, 1024); run(gate, fade, 0, 1024);
    float maxStep = 0;
    for (int i = 1; i < 1024; ++i) maxStep = std::max(maxStep, std::fabs(fade[0][size_t(i)] - fade[0][size_t(i - 1)]));
    check(fade[0].back() == 0.001f && gate.reductionDb() == 0 && maxStep < 4e-6f,
          "gate: range zero fades smoothly to bit-exact dry within 20 ms");
    gate.reset(); auto untouched = constant(0.075f, 64, -0.125f); const auto originalUntouched = untouched;
    run(gate, untouched, 0, 64);
    check(untouched == originalUntouched, "gate: reset with zero range is exactly transparent");

    std::printf("\n[three-band dynamic EQ]\n");
    DynamicEq eq;
    eq.prepare(48000, 2);
    DynamicEq::Params ep;
    ep.band[0].frequencyHz = 4000; ep.band[0].q = 4; ep.band[0].thresholdDb = -40;
    ep.band[0].ratio = 12; ep.band[0].maxReductionDb = 9; ep.band[0].attackMs = 0.1f;
    ep.band[1].enabled = ep.band[2].enabled = 0;
    eq.setParams(ep); eq.reset();
    constexpr int warm = 12000, measured = 12000, total = warm + measured;
    auto selected = blank(total);
    for (int i = 0; i < total; ++i) {
        selected[0][size_t(i)] = 0.2f * float(std::sin(2 * pi * 4000 * i / 48000.0))
            + 0.1f * float(std::sin(2 * pi * 100 * i / 48000.0));
        selected[1][size_t(i)] = selected[0][size_t(i)] * 0.01f;
    }
    const auto originalSelected = selected;
    run(eq, selected, 0, total);
    const double centerDb = 20 * std::log10(amplitude(selected[0], 4000, warm, measured)
        / amplitude(originalSelected[0], 4000, warm, measured));
    const double farDb = 20 * std::log10(amplitude(selected[0], 100, warm, measured)
        / amplitude(originalSelected[0], 100, warm, measured));
    imageError = 0;
    for (int i = warm; i < total; ++i)
        imageError = std::max(imageError, std::fabs(selected[1][size_t(i)] - selected[0][size_t(i)] * 0.01f));
    std::printf("  4 kHz center %.3f dB; 100 Hz %.4f dB; applied center reduction %.3f dB\n",
                centerDb, farDb, eq.reductionDb(0));
    check(std::fabs(centerDb + 9) < 0.08 && std::fabs(farDb) < 0.02,
          "dynamic EQ: normalized center reaches the 9 dB cap while a distant tone is preserved");
    check(imageError < 2e-7f && eq.reductionDb(0) <= 9.001f && eq.reductionDb(1) == 0,
          "dynamic EQ: shared per-band attenuation preserves a 100:1 channel ratio");

    const auto fullParams = ep;
    ep.band[0].thresholdDb = 0; eq.setParams(ep); eq.reset();
    auto belowThreshold = originalSelected; run(eq, belowThreshold, 0, total);
    check(belowThreshold == originalSelected && eq.reductionDb(0) == 0,
          "dynamic EQ: an enabled band below threshold leaves audio bit-exact");
    for (int mode = 0; mode < 3; ++mode) {
        ep = fullParams;
        if (mode == 0) ep.band[0].ratio = 1;
        if (mode == 1) ep.band[0].maxReductionDb = 0;
        if (mode == 2) ep.band[0].enabled = 0;
        eq.setParams(ep); eq.reset();
        auto transparent = originalSelected; run(eq, transparent, 0, total);
        check(transparent == originalSelected && eq.reductionDb(0) == 0,
              mode == 0 ? "dynamic EQ: ratio one is bit-exact transparent after reset"
              : mode == 1 ? "dynamic EQ: zero range is bit-exact transparent after reset"
                          : "dynamic EQ: disabled bands are bit-exact transparent after reset");
    }
    ep = fullParams; eq.setParams(ep); eq.reset();
    selected = originalSelected; run(eq, selected, 0, warm);
    ep.band[0].enabled = 0; eq.setParams(ep);
    run(eq, selected, warm, total - warm);
    bool exactTail = true;
    for (int c = 0; c < 2; ++c)
        for (int i = warm + 960; i < total; ++i)
            exactTail = exactTail && selected[size_t(c)][size_t(i)] == originalSelected[size_t(c)][size_t(i)];
    check(exactTail && eq.reductionDb(0) == 0, "dynamic EQ: running disable reaches exact dry after its finite 20 ms fade");
    auto silence = blank(2048); ep.band[0].enabled = 1; eq.setParams(ep);
    run(eq, silence, 0, 2048);
    check(silence == blank(2048), "dynamic EQ: re-enabling a settled disabled band cannot replay old filter state");

    std::printf("\n[gate/dynamic EQ real-time invariants]\n");
    auto source = blank(12000);
    for (int i = 0; i < 12000; ++i) {
        source[0][size_t(i)] = 0.2f * float(std::sin(2 * pi * 1571 * i / 48000.0));
        source[1][size_t(i)] = 0.1f * float(std::sin(2 * pi * 173 * i / 48000.0));
    }
    NoiseGate gateWhole, gateChunks;
    DynamicEq eqWhole, eqChunks;
    gateWhole.prepare(48000, 2); gateChunks.prepare(48000, 2);
    eqWhole.prepare(48000, 2); eqChunks.prepare(48000, 2);
    auto whole = source, chunks = source;
    NoiseGate::Params lastGate;
    DynamicEq::Params lastEq;
    const int divisions[]{0, 2048, 4096, 12000};
    for (int part = 0; part < 3; ++part) {
        NoiseGate::Params g;
        DynamicEq::Params e;
        g.thresholdDb = part == 0 ? -20.0f : -45.0f;
        g.rangeDb = part == 1 ? 0.0f : 60.0f;
        e.band[0].frequencyHz = part == 0 ? 200.0f : 3800.0f;
        e.band[1].q = part == 1 ? 8.0f : 0.3f;
        e.band[2].enabled = part == 1 ? 0u : 1u;
        lastGate = g; lastEq = e;
        gateWhole.setParams(g); gateChunks.setParams(g);
        eqWhole.setParams(e); eqChunks.setParams(e);
        const int begin = divisions[part], end = divisions[part + 1];
        run(gateWhole, whole, begin, end - begin); run(eqWhole, whole, begin, end - begin);
        for (int pos = begin; pos < end;) {
            const int count = std::min(1 + ((pos * 17) % 127), end - pos);
            run(gateChunks, chunks, pos, count); run(eqChunks, chunks, pos, count);
            pos += count;
        }
    }
    check(whole == chunks, "gate/dynamic EQ: parameter events and sample history are invariant to block partitioning");
    gateWhole.reset(); eqWhole.reset();
    NoiseGate freshGate; DynamicEq freshEq;
    freshGate.prepare(48000, 2); freshEq.prepare(48000, 2);
    freshGate.setParams(lastGate); freshEq.setParams(lastEq);
    whole = source; chunks = source;
    run(gateWhole, whole, 0, 12000); run(eqWhole, whole, 0, 12000);
    run(freshGate, chunks, 0, 12000); run(freshEq, chunks, 0, 12000);
    check(whole == chunks, "gate/dynamic EQ: reset removes all envelope, filter, hold and ramp history");

    bool ratesAndSweeps = true, recovery = true;
    float sweepPeak = 0;
    for (double rate : {8000.0, 11025.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
        NoiseGate g; DynamicEq e;
        g.prepare(rate, 8); e.prepare(rate, 8);
        ratesAndSweeps = ratesAndSweeps && g.supported() && e.supported()
            && g.latencySamples() == 0 && e.latencySamples() == 0;
        std::array<std::array<float, 97>, 8> data{};
        float *pointers[8]{};
        for (int c = 0; c < 8; ++c) pointers[c] = data[size_t(c)].data();
        for (int block = 0; block < 180; ++block) {
            NoiseGate::Params ng; DynamicEq::Params de;
            ng.thresholdDb = block & 1 ? -80.0f : 0.0f; ng.hysteresisDb = block & 1 ? 18.0f : 0.0f;
            ng.rangeDb = block % 3 ? 80.0f : 0.0f; ng.attackMs = block & 1 ? 0.1f : 50.0f;
            ng.holdMs = block & 1 ? 0.0f : 500.0f; ng.releaseMs = block & 1 ? 10.0f : 1000.0f;
            for (int b = 0; b < DynamicEq::kBands; ++b) {
                auto &p = de.band[b];
                p.frequencyHz = (block + b) & 1 ? 16000.0f : 40.0f;
                p.q = block & 1 ? 8.0f : 0.3f; p.thresholdDb = -60;
                p.ratio = 12; p.maxReductionDb = 18; p.attackMs = 0.1f; p.releaseMs = 1000;
            }
            if (block == 20) {
                ng.thresholdDb = std::numeric_limits<float>::quiet_NaN();
                ng.hysteresisDb = std::numeric_limits<float>::infinity(); ng.rangeDb = -1e30f;
                ng.attackMs = -1e30f; ng.holdMs = 1e30f; ng.releaseMs = -std::numeric_limits<float>::infinity();
                for (auto &p : de.band) {
                    p.frequencyHz = std::numeric_limits<float>::quiet_NaN();
                    p.q = std::numeric_limits<float>::infinity(); p.thresholdDb = -1e30f;
                    p.ratio = -std::numeric_limits<float>::infinity(); p.maxReductionDb = 1e30f;
                    p.attackMs = std::numeric_limits<float>::quiet_NaN(); p.releaseMs = -1e30f;
                    p.enabled = 0xffffffffu;
                }
            }
            g.setParams(ng); e.setParams(de);
            for (int c = 0; c < 8; ++c)
                for (int i = 0; i < 97; ++i)
                    data[size_t(c)][size_t(i)] = 0.8f * float(std::sin((block * 97 + i) * (0.017 + c * 0.103)));
            if (block == 21) {
                data[0][0] = std::numeric_limits<float>::quiet_NaN(); data[1][1] = std::numeric_limits<float>::infinity();
                data[2][2] = -std::numeric_limits<float>::infinity(); data[3][3] = std::numeric_limits<float>::max();
            }
            g.process({pointers, 8, 97}); e.process({pointers, 8, 97});
            for (const auto &channel : data)
                for (float v : channel) {
                    ratesAndSweeps = ratesAndSweeps && finite(v) && std::fabs(v) <= kRail;
                    if (block >= 24) sweepPeak = std::max(sweepPeak, std::fabs(v));
                }
        }
        // Independent invalid input to EQ, rather than relying on the gate to sanitize it.
        data[0][0] = std::numeric_limits<float>::quiet_NaN();
        data[1][0] = std::numeric_limits<float>::infinity();
        e.process({pointers, 8, 97});
        for (const auto &channel : data) for (float v : channel) recovery = recovery && finite(v);
        NoiseGate::Params transparentGate; transparentGate.rangeDb = 0;
        DynamicEq::Params transparentEq; for (auto &p : transparentEq.band) p.enabled = 0;
        g.setParams(transparentGate); e.setParams(transparentEq); g.reset(); e.reset();
        for (auto &channel : data) channel.fill(0.1f);
        g.process({pointers, 8, 97}); e.process({pointers, 8, 97});
        for (const auto &channel : data) for (float v : channel) recovery = recovery && v == 0.1f;
        pointers[3] = nullptr;
        g.process({pointers, 8, 97}); e.process({pointers, 8, 97});
        g.process({nullptr, 8, 97}); e.process({nullptr, 8, 97});
        g.process({pointers, 8, 0}); e.process({pointers, 8, 0});
    }
    check(ratesAndSweeps, "gate/dynamic EQ: 8-192 kHz, eight channels, extreme sweeps and nonfinite controls remain bounded");
    std::printf("  finite-input sweep peak after fault recovery: %.4f (rail %.1f)\n", sweepPeak, kRail);
    check(sweepPeak < 2.0f, "gate/dynamic EQ: rapid frequency/Q sweeps stay stable without relying on the output rail");
    check(recovery, "gate/dynamic EQ: NaN/Inf input recovers, reset clears contamination, null channels are safe");

    bool unsupported = true;
    for (double rate : {0.0, 7999.0, 192001.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        NoiseGate g; DynamicEq e; g.prepare(rate, 2); e.prepare(rate, 2);
        auto audio = source; g.process({}); e.process({});
        run(g, audio, 0, 12000); run(e, audio, 0, 12000);
        unsupported = unsupported && !g.supported() && !e.supported() && audio == source;
    }
    for (int channels : {0, 9}) {
        NoiseGate g; DynamicEq e; g.prepare(48000, channels); e.prepare(48000, channels);
        unsupported = unsupported && !g.supported() && !e.supported();
    }
    check(unsupported, "gate/dynamic EQ: unsupported rates/channel counts explicitly bypass without changing audio");
}

// Caller prepares both effects for 48 kHz/eight channels, then enables its
// allocation counter around this helper. Fixed stack buffers; no test vectors.
inline void exerciseGateDynamicEqRealtime(dsp::NoiseGate &gate, dsp::DynamicEq &eq)
{
    std::array<std::array<float, 64>, 8> data{};
    float *channels[8]{};
    for (int c = 0; c < 8; ++c) channels[c] = data[size_t(c)].data();
    for (int block = 0; block < 256; ++block) {
        dsp::NoiseGate::Params gp; dsp::DynamicEq::Params ep;
        gp.thresholdDb = -60.0f + float(block % 60); gp.rangeDb = block % 31 ? 60.0f : 0.0f;
        gp.holdMs = float(block % 500); gp.releaseMs = 10.0f + float(block % 990);
        for (int b = 0; b < dsp::DynamicEq::kBands; ++b) {
            ep.band[b].frequencyHz = 40.0f + float((block * 137 + b * 791) % 15960);
            ep.band[b].q = 0.3f + float(block % 78) * 0.1f;
            ep.band[b].enabled = block % 29 ? 1u : 0u;
            ep.band[b].thresholdDb = -42; ep.band[b].ratio = 4;
        }
        gate.setParams(gp); eq.setParams(ep);
        for (int c = 0; c < 8; ++c)
            for (int i = 0; i < 64; ++i)
                data[size_t(c)][size_t(i)] = 0.2f * float(std::sin((block * 64 + i) * (0.07 + c * 0.01)));
        gate.process({channels, 8, 64}); eq.process({channels, 8, 64});
    }
}
} // namespace dreamdsp::tests
