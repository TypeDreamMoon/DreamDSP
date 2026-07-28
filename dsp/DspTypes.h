#pragma once

#include <cmath>
#include <cstddef>

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

} // namespace dreamdsp::dsp
