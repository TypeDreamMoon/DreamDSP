#pragma once

#include "DspTypes.h"
#include "ParameterSmoother.h"

#include <array>

namespace dreamdsp::dsp {

// Three serial, downward dynamic bell filters. Each normalized SVF bandpass
// has unity center gain; dry + (gain - 1) * band is unity at DC/Nyquist and
// has the specified cut at its center. All channels share each band's detector.
class DynamicEq
{
public:
    static constexpr int kBands = 3;
    struct Band {
        float frequencyHz = 200.0f;   // 40..16000, also clamped to 0.45 * fs
        float q = 1.0f;               // 0.3..8, detector/filter selectivity
        float thresholdDb = -24.0f;   // -60..0, band peak dBFS (10 ms detector decay)
        float ratio = 2.0f;           // 1..12, downward compression above threshold
        float maxReductionDb = 6.0f;  // 0..18, maximum CENTER gain reduction
        float attackMs = 10.0f;       // 0.1..100, attenuation time constant
        float releaseMs = 120.0f;     // 10..1000, attenuation time constant
        uint32_t enabled = 1;
    };
    struct Params { Band band[kBands]{{200.0f}, {1000.0f}, {6000.0f}}; };
    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &params) noexcept;
    void process(const AudioBuffer &buffer) noexcept;
    bool supported() const noexcept { return m_supported; }
    int latencySamples() const noexcept { return 0; } // no lookahead/buffering
    // Positive applied center attenuation; audio-thread-owned state.
    float reductionDb(int band) const noexcept;

private:
    static constexpr int kMaxChannels = 8;
    struct State {
        std::array<float, kMaxChannels> band{}, low{};
        ParameterSmoother g, r, threshold, slope, limit, attack, release;
        float targetG = 0, targetR = 1, targetSlope = 0, targetAttack = 0, targetRelease = 0;
        float detector = 0, reduction = 0, appliedReduction = 0;
        float blend = 0, blendTarget = 0, blendStep = 0;
        int blendRemaining = 0;
    };
    Params m_p;
    std::array<State, kBands> m_state;
    double m_rate = 48000.0;
    int m_channels = 0, m_rampSamples = 960;
    float m_detectorRelease = 0;
    bool m_supported = false, m_started = false;
};

static_assert(sizeof(DynamicEq::Band) == 32, "Dynamic EQ band wire layout");
static_assert(sizeof(DynamicEq::Params) == 96, "Dynamic EQ has three wire bands");
} // namespace dreamdsp::dsp
