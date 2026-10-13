#pragma once

#include "TapeSaturation.h"
#include "DeEsser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace dreamdsp::tests {

template <class Check>
void runTapeDeEsserTests(Check &&check)
{
    using namespace dsp;
    using Stereo = std::array<std::vector<float>, 2>;
    constexpr double pi = 3.14159265358979323846;
    constexpr int frames = 16384, warm = 4096;
    const auto blank = [](int n) { return Stereo{std::vector<float>(size_t(n)), std::vector<float>(size_t(n))}; };
    const auto run = [](auto &effect, Stereo &audio, int start, int count) {
        float *channels[] = {audio[0].data() + start, audio[1].data() + start};
        effect.process({channels, 2, count});
    };
    const auto finite = [](float value) {
        uint32_t bits; std::memcpy(&bits, &value, sizeof bits);
        return (bits & 0x7F800000u) != 0x7F800000u;
    };
    const auto near = [](const Stereo &a, const Stereo &b, float tolerance) {
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < a[size_t(c)].size(); ++i)
                if (std::fabs(a[size_t(c)][i] - b[size_t(c)][i]) > tolerance) return false;
        return true;
    };
    const auto amplitude = [&](const std::vector<float> &audio, double frequency, double rate = 48000.0) {
        double real = 0, imag = 0;
        for (int i = warm; i < warm + frames; ++i) {
            const double phase = 2.0 * pi * frequency * i / rate;
            real += audio[size_t(i)] * std::cos(phase);
            imag -= audio[size_t(i)] * std::sin(phase);
        }
        return 2.0 * std::sqrt(real * real + imag * imag) / frames;
    };
    const auto sine = [&](double frequency, float gain) {
        auto audio = blank(frames + warm);
        for (int i = 0; i < frames + warm; ++i)
            audio[0][size_t(i)] = audio[1][size_t(i)] = gain * float(std::sin(2 * pi * frequency * i / 48000.0));
        return audio;
    };

    std::printf("\n[tape saturation]\n");
    TapeSaturation tape;
    tape.prepare(48000, 2);
    TapeSaturation::Params tp;
    tp.driveDb = 12; tp.memory = 0; tp.bumpDb = 0; tp.toneHz = 20000;
    tape.setParams(tp); tape.reset();
    constexpr double fundamental = 48000.0 * 341.0 / frames;
    auto harmonic = sine(fundamental, 0.35f);
    run(tape, harmonic, 0, frames + warm);
    const double h1 = amplitude(harmonic[0], fundamental), h3 = amplitude(harmonic[0], 3 * fundamental);
    check(h3 / h1 > 0.015 && h3 / h1 < 0.5, "tape: nonlinear recording curve generates bounded odd harmonics");
    check(harmonic[0] == harmonic[1], "tape: shared parameter smoothing preserves identical stereo channels");

    constexpr double aliasFundamental = 48000.0 * 3901.0 / frames;
    const double foldedThird = 48000.0 - 3 * aliasFundamental;
    auto aliased = sine(aliasFundamental, 0.65f), oversampled = aliased;
    for (auto &channel : aliased)
        for (float &sample : channel) sample = std::tanh(dbToLin(tp.driveDb) * sample);
    tape.reset(); run(tape, oversampled, 0, frames + warm);
    const double rawAlias = amplitude(aliased[0], foldedThird) / amplitude(aliased[0], aliasFundamental);
    const double osAlias = amplitude(oversampled[0], foldedThird) / amplitude(oversampled[0], aliasFundamental);
    const double aliasImprovement = 20 * std::log10((rawAlias + 1e-15) / (osAlias + 1e-15));
    std::printf("  folded third harmonic: 1x %.2f dBc, tape 4x %.2f dBc; improvement %.2f dB\n",
                20 * std::log10(rawAlias + 1e-15), 20 * std::log10(osAlias + 1e-15), aliasImprovement);
    check(aliasImprovement > 25, "tape: 4x processing suppresses the folded third harmonic versus a 1x shaper");

    TapeSaturation dryEffect, wetEffect, halfEffect;
    dryEffect.prepare(48000, 2); wetEffect.prepare(48000, 2); halfEffect.prepare(48000, 2);
    tp.mix = 0; dryEffect.setParams(tp); dryEffect.reset();
    tp.mix = 1; wetEffect.setParams(tp); wetEffect.reset();
    tp.mix = 0.5f; halfEffect.setParams(tp); halfEffect.reset();
    auto dry = sine(15000, 0.12f), wet = dry, half = dry;
    run(dryEffect, dry, 0, frames + warm); run(wetEffect, wet, 0, frames + warm);
    run(halfEffect, half, 0, frames + warm);
    float blendError = 0;
    for (int i = 0; i < frames + warm; ++i)
        blendError = std::max(blendError, std::fabs(half[0][size_t(i)] - 0.5f * (dry[0][size_t(i)] + wet[0][size_t(i)])));
    check(blendError < 2e-5f, "tape: half mix equals the aligned dry/wet average through one 4x path");

    tp.driveDb = 0; tp.mix = 1; tp.memory = 0; tp.bumpDb = 0; tp.toneHz = 20000;
    tape.setParams(tp); tape.reset();
    auto bright = sine(12000, 0.01f); run(tape, bright, 0, frames + warm);
    tp.toneHz = 2000; tape.setParams(tp); tape.reset();
    auto dark = sine(12000, 0.01f); run(tape, dark, 0, frames + warm);
    check(amplitude(dark[0], 12000) < amplitude(bright[0], 12000) * 0.3,
          "tape: tone control softens high frequencies in the wet path");
    tp.toneHz = 20000; tp.bumpDb = 0; tape.setParams(tp); tape.reset();
    auto flatLow = sine(65, 0.005f); run(tape, flatLow, 0, frames + warm);
    tp.bumpDb = 6; tape.setParams(tp); tape.reset();
    auto bumpedLow = sine(65, 0.005f); run(tape, bumpedLow, 0, frames + warm);
    const double bumpGain = 20 * std::log10(amplitude(bumpedLow[0], 65) / amplitude(flatLow[0], 65));
    check(bumpGain > 5.5 && bumpGain < 6.5, "tape: head-bump parameter gives the stated small-signal 65 Hz lift");
    tp.driveDb = 12; tp.bumpDb = 0; tp.memory = 0; tape.setParams(tp); tape.reset();
    auto staticCurve = sine(200, 0.25f); run(tape, staticCurve, 0, frames + warm);
    tp.memory = 1; tape.setParams(tp); tape.reset();
    auto remembered = sine(200, 0.25f); run(tape, remembered, 0, frames + warm);
    check(!near(staticCurve, remembered, 1e-3f), "tape: memory/sag changes stateful dynamics beyond the static shaper");

    std::printf("\n[selective de-esser]\n");
    DeEsser deesser;
    deesser.prepare(48000, 2);
    DeEsser::Params dp;
    dp.frequencyHz = 3500; dp.thresholdDb = -35; dp.ratio = 12; dp.maxReductionDb = 12;
    dp.attackMs = 0.1f;
    deesser.setParams(dp); deesser.reset();
    auto mixed = blank(frames + warm);
    for (int i = 0; i < frames + warm; ++i) {
        mixed[0][size_t(i)] = 0.15f * float(std::sin(2 * pi * 375 * i / 48000.0))
            + 0.4f * float(std::sin(2 * pi * 9000 * i / 48000.0));
        mixed[1][size_t(i)] = mixed[0][size_t(i)] * 0.01f;
    }
    const auto originalMixed = mixed;
    run(deesser, mixed, 0, frames + warm);
    const double highDb = 20 * std::log10(amplitude(mixed[0], 9000) / amplitude(originalMixed[0], 9000));
    const double lowDb = 20 * std::log10(amplitude(mixed[0], 375) / amplitude(originalMixed[0], 375));
    std::printf("  high-band probe %.2f dB; low-band probe %.3f dB; detector reduction %.2f dB\n",
                highDb, lowDb, deesser.reductionDb());
    check(highDb < -6 && std::fabs(lowDb) < 0.2,
          "de-esser: attenuates a sibilant band while preserving the lower vocal band");
    float imageError = 0;
    for (int i = warm; i < frames + warm; ++i)
        imageError = std::max(imageError, std::fabs(mixed[0][size_t(i)] * 0.01f - mixed[1][size_t(i)]));
    check(imageError < 2e-6f && deesser.reductionDb() <= dp.maxReductionDb,
          "de-esser: loud-channel detector applies one bounded gain to quiet and loud channels");
    auto releaseLead = blank(64); run(deesser, releaseLead, 0, 64);
    const float reductionBeforeRelease = deesser.reductionDb();
    auto releaseSilence = blank(3840); run(deesser, releaseSilence, 0, 3840);
    const float releaseFraction = deesser.reductionDb() / reductionBeforeRelease;
    check(reductionBeforeRelease > 1 && releaseFraction > 0.36f && releaseFraction < 0.38f,
          "de-esser: silent input drains the linked reduction by its 80 ms release time constant");
    dp.ratio = 1; deesser.setParams(dp); deesser.reset();
    auto neutral = originalMixed; run(deesser, neutral, 0, frames + warm);
    check(near(neutral, originalMixed, 0), "de-esser: ratio one reconstructs the original samples exactly");
    dp.ratio = 12; dp.thresholdDb = 0; deesser.setParams(dp); deesser.reset();
    neutral = originalMixed; run(deesser, neutral, 0, frames + warm);
    check(near(neutral, originalMixed, 0), "de-esser: material below threshold remains bit-exact");

    // Same sample history and parameter-update point, unrelated block sizes.
    TapeSaturation tapeWhole, tapeSplit;
    DeEsser deessWhole, deessSplit;
    tapeWhole.prepare(48000, 2); tapeSplit.prepare(48000, 2);
    deessWhole.prepare(48000, 2); deessSplit.prepare(48000, 2);
    auto wa = blank(32), wb = wa;
    run(tapeWhole, wa, 0, 32); run(tapeSplit, wb, 0, 32);
    run(deessWhole, wa, 0, 32); run(deessSplit, wb, 0, 32);
    tp.driveDb = 22; tp.toneHz = 2300; tp.bumpDb = 5; tp.memory = 0.8f; tp.mix = 0.73f;
    dp.frequencyHz = 2200; dp.thresholdDb = -40; dp.ratio = 8; dp.attackMs = 4;
    tapeWhole.setParams(tp); tapeSplit.setParams(tp); deessWhole.setParams(dp); deessSplit.setParams(dp);
    auto whole = originalMixed, split = originalMixed;
    run(tapeWhole, whole, 0, frames + warm); run(deessWhole, whole, 0, frames + warm);
    for (int offset = 0; offset < frames + warm;) {
        const int block = std::min(1 + (offset * 17 + 11) % 197, frames + warm - offset);
        run(tapeSplit, split, offset, block); run(deessSplit, split, offset, block); offset += block;
    }
    check(near(whole, split, 2e-6f), "tape/de-esser: parameter slews and histories are invariant to block partitioning");

    bool safe = true;
    const uint32_t malformedBits[] = {0x7FC12345u, 0x7F800000u, 0xFF800000u};
    for (double rate : {8000.0, 44100.0, 96000.0, 192000.0}) {
        tape.prepare(rate, 8); deesser.prepare(rate, 8);
        for (uint32_t bits : malformedBits) {
            float malformed; std::memcpy(&malformed, &bits, sizeof bits);
            TapeSaturation::Params badTape;
            badTape.driveDb = badTape.toneHz = badTape.bumpDb = badTape.memory = badTape.mix = badTape.outputDb = malformed;
            DeEsser::Params badDeess;
            badDeess.frequencyHz = badDeess.thresholdDb = badDeess.ratio = badDeess.maxReductionDb
                = badDeess.attackMs = badDeess.releaseMs = malformed;
            tape.setParams(badTape); deesser.setParams(badDeess); tape.reset(); deesser.reset();
            float storage[8][64]{}; float *channels[8];
            for (int c = 0; c < 8; ++c) { channels[c] = storage[c]; storage[c][0] = malformed; storage[c][1] = 0.1f; }
            tape.process({channels, 8, 64}); deesser.process({channels, 8, 64});
            for (const auto &channel : storage) for (float value : channel)
                safe &= finite(value) && std::fabs(value) <= 4;
        }
    }
    check(safe, "tape/de-esser: malformed parameters and samples recover with finite bounded output at 8..192 kHz");
    bool unsupported = true;
    for (double rate : {4000.0, 384000.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        tape.prepare(rate, 2); deesser.prepare(rate, 2);
        auto audio = originalMixed; run(tape, audio, 0, frames + warm); run(deesser, audio, 0, frames + warm);
        unsupported &= !tape.supported() && !deesser.supported() && near(audio, originalMixed, 0);
    }
    check(unsupported, "tape/de-esser: unsupported rates are declared and pass through untouched");

    TapeSaturation faulted, reference;
    faulted.prepare(48000, 2); reference.prepare(48000, 2);
    auto faultedWarm = sine(1500, 0.12f), referenceWarm = faultedWarm;
    run(faulted, faultedWarm, 0, frames + warm); run(reference, referenceWarm, 0, frames + warm);
    auto faultedNext = blank(128), referenceNext = faultedNext;
    for (int i = 0; i < 128; ++i) {
        faultedNext[0][size_t(i)] = referenceNext[0][size_t(i)] = 0.05f;
        faultedNext[1][size_t(i)] = referenceNext[1][size_t(i)] = 0.1f * float(std::sin(i * 0.31));
    }
    faultedNext[0][0] = std::numeric_limits<float>::quiet_NaN();
    run(faulted, faultedNext, 0, 128); run(reference, referenceNext, 0, 128);
    bool isolated = faultedNext[1] == referenceNext[1];
    for (float value : faultedNext[0]) isolated &= finite(value);
    check(isolated, "tape: invalid input resets only its channel without changing the other channel's history");
    tape.prepare(8000, 1); deesser.prepare(8000, 1);
    float mono[256]{}; mono[0] = 0.25f; float *monoChannel[] = {mono};
    tape.process({monoChannel, 1, 256}); deesser.process({monoChannel, 1, 256});
    bool validMono = tape.supported() && deesser.supported();
    for (float value : mono) validMono &= finite(value) && std::fabs(value) <= 4;
    check(validMono, "tape/de-esser: mono processing works at the 8 kHz lower rate boundary");
}

// Prepare both effects first, then wrap this fixed-storage exercise in the
// harness allocation counter. Parameter setters and processing allocate zero.
inline void exerciseTapeDeEsserRealtime(dsp::TapeSaturation &tape, dsp::DeEsser &deesser) noexcept
{
    float storage[8][64]{}; float *channels[8];
    for (int c = 0; c < 8; ++c) channels[c] = storage[c];
    for (int block = 0; block < 256; ++block) {
        dsp::TapeSaturation::Params tp;
        tp.driveDb = float(block % 25); tp.toneHz = 2000 + float(block % 19) * 1000;
        tp.mix = float(block % 11) * 0.1f;
        dsp::DeEsser::Params dp;
        dp.frequencyHz = 2000 + float(block % 11) * 1000; dp.thresholdDb = -float(block % 61);
        tape.setParams(tp); deesser.setParams(dp);
        for (int c = 0; c < 8; ++c)
            for (int i = 0; i < 64; ++i) storage[c][i] = 0.2f * float((i + block + c) % 31) / 31;
        tape.process({channels, 8, 64}); deesser.process({channels, 8, 64});
    }
}
} // namespace dreamdsp::tests
