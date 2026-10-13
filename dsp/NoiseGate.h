#pragma once

#include "DspTypes.h"
#include "ParameterSmoother.h"

namespace dreamdsp::dsp {

// Linked, downward gate without lookahead. Hold counts consecutive samples
// below the closing threshold AFTER the 10 ms peak-detector release.
class NoiseGate
{
public:
    struct Params {
        float thresholdDb = -50.0f;   // -80..0 dBFS: opens at this level
        float hysteresisDb = 6.0f;    // 0..18: closes below threshold - hysteresis
        float rangeDb = 60.0f;        // 0..80: maximum closed-gate attenuation
        float attackMs = 5.0f;        // 0.1..50: gain-rise time constant
        float holdMs = 80.0f;         // 0..500: continuous quiet time before closing
        float releaseMs = 150.0f;     // 10..1000: gain-fall time constant
    };
    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &params) noexcept;
    void process(const AudioBuffer &buffer) noexcept;
    bool supported() const noexcept { return m_supported; }
    int latencySamples() const noexcept { return 0; }
    // Audio-thread-owned, positive dB of applied attenuation.
    float reductionDb() const noexcept { return m_reduction; }

private:
    static constexpr int kMaxChannels = 8;
    Params m_p;
    ParameterSmoother m_threshold, m_hysteresis, m_floor, m_attack, m_hold, m_release;
    double m_rate = 48000.0;
    int m_channels = 0, m_quietSamples = 0, m_rampSamples = 960, m_bypassRemaining = 0;
    bool m_supported = false, m_started = false, m_open = false;
    float m_detector = 0, m_detectorRelease = 0, m_gain = 1, m_reduction = 0;
    float m_targetThreshold = 0, m_targetHysteresis = 0, m_targetFloor = 0;
    float m_targetAttack = 0, m_targetHold = 0, m_targetRelease = 0;
    float m_bypass = 0, m_bypassTarget = 0, m_bypassStep = 0;
};

static_assert(sizeof(NoiseGate::Params) == 24, "Gate parameters are six wire floats");
} // namespace dreamdsp::dsp
