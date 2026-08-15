#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// The DSP layer is deliberately free of Windows, Qt and any I/O.
//
// Everything here has to be able to run inside a real-time audio callback one
// day, which means: no allocation, no locks, no logging, no exceptions on the
// process path. Keeping the layer portable is not about portability -- it is
// what makes the code testable offline, before it is ever loaded into
// audiodg.exe where a mistake costs the machine its sound.

namespace dreamdsp::dsp {

// Deinterleaved, which is the format Windows APOs receive and the only sane
// layout for per-channel processing.
struct AudioBuffer {
    float *const *channels = nullptr;
    int channelCount = 0;
    int frames = 0;

    bool valid() const { return channels && channelCount > 0 && frames > 0; }
};

inline float dbToLin(float db) { return std::pow(10.0f, db * 0.05f); }

// Floored so a digital-silence sample yields a large negative number rather
// than -inf, which would poison every downstream smoother.
inline float linToDb(float lin)
{
    constexpr float kEps = 1e-9f;
    return 20.0f * std::log10(std::fabs(lin) + kEps);
}

// One-pole smoothing coefficient for a time constant, using the convention
// that `ms` is the time to reach 1 - 1/e of a step.
inline float timeConstant(float ms, double sampleRate)
{
    if (ms <= 0.0f || sampleRate <= 0.0)
        return 0.0f;
    return std::exp(-1.0f / (float(sampleRate) * ms * 0.001f));
}

template <typename T>
inline T clampTo(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The ceiling every stage that can produce arbitrary gain writes through:
// +12 dBFS.
//
// An equalizer, a channel matrix and a preamp are all unbounded-gain devices by
// definition -- thirty-two bands at +40 dB, or eight inputs summed at full
// weight, are legitimate parameter sets, and no validator can tell them apart
// from ones the user meant. This runs inside audiodg.exe with a file under
// ProgramData as its input. A post-mix object's output goes straight to the
// endpoint, where anything above 0 dBFS is clipped away regardless, so a rail
// four times higher cannot remove anything that would have been heard, and does
// stop a hostile or simply mistaken parameter set from becoming a blast of
// full-scale noise.
constexpr float kRail = 4.0f;

// Bounds one sample, reporting whether it had to.
//
// The exponent test is not decoration. Comparisons cannot do this job: the DSP
// layer is built with /fp:fast, under which the compiler is entitled to assume
// no operand is ever NaN and fold the test away. A NaN that reaches a recursive
// filter's state stays there until the stream is torn down, so it has to be
// stopped at the first stage that can see it.
inline float railed(double v, bool *bad) noexcept
{
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    if ((bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull) {
        *bad = true;
        return 0.0f;
    }
    return v < -double(kRail) ? -kRail : (v > double(kRail) ? kRail : float(v));
}

inline float railed(float v, bool *bad) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    if ((bits & 0x7F800000u) == 0x7F800000u) {
        *bad = true;
        return 0.0f;
    }
    return v < -kRail ? -kRail : (v > kRail ? kRail : v);
}

} // namespace dreamdsp::dsp
