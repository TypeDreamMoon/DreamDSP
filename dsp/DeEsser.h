#pragma once

#include "DspTypes.h"
#include "ParameterSmoother.h"

#include <array>

namespace dreamdsp::dsp {

// Selective high-frequency dynamics, not a speech/phoneme classifier. A
// complementary first-order split sums exactly to dry at unity band gain.
// One detector/gain envelope links ALL configured channels to retain image.
class DeEsser
{
public:
    struct Params {
        float frequencyHz = 6000.0f;   // 2000..12000, onset of high-band selection
        float thresholdDb = -24.0f;    // -60..0 dBFS, rectified high-band detector
        float ratio = 4.0f;            // 1..12, compression above threshold
        float maxReductionDb = 9.0f;   // 0..18, cap on HIGH-BAND gain reduction
        float attackMs = 1.0f;         // 0.1..20, attenuation attack time constant
        float releaseMs = 80.0f;       // 10..500, attenuation release time constant
    };
    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &params) noexcept;
    void process(const AudioBuffer &buffer) noexcept;
    bool supported() const noexcept { return m_supported; }
    int latencySamples() const noexcept { return 0; } // causal, no lookahead
    // Positive dB of current high-band reduction; audio-thread-owned state.
    float reductionDb() const noexcept { return m_reduction; }

private:
    static constexpr int kMaxChannels = 8;
    Params m_p;
    std::array<float, kMaxChannels> m_lowState{};
    ParameterSmoother m_split, m_threshold, m_slope, m_limit, m_attack, m_release;
    double m_rate = 48000.0;
    int m_channels = 0;
    bool m_supported = false, m_started = false;
    float m_reduction = 0;
    float m_targetSplit = 0, m_targetSlope = 0, m_targetAttack = 0, m_targetRelease = 0;
};

static_assert(sizeof(DeEsser::Params) == 24, "De-esser parameters are six wire floats");
} // namespace dreamdsp::dsp
