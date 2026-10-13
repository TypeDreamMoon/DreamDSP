#pragma once
#include <array>
#include <vector>
#include "DspTypes.h"
#include "ParameterSmoother.h"

namespace dreamdsp::dsp {
class Modulation
{
public:
    enum class Kind { Chorus, Flanger, Phaser };
    struct Params {
        float rateHz = 0.35f;
        float depth = 0.5f;
        float delayMs = 12.0f;
        float feedback = 0.15f;
        float mix = 0.35f;
        float stereoPhase = 0.25f;
    };
    explicit Modulation(Kind kind) : m_kind(kind) {}
    // Invalid/out-of-range rates fall back to 48 kHz; supported range 8--384 kHz.
    void prepare(double rate, int channels);
    void reset() noexcept;
    // Defends the direct API against non-finite parameters, including /fp:fast.
    void setParams(const Params &p) noexcept;
    void process(const AudioBuffer &buf) noexcept;
private:
    static constexpr int kChannels = 8;
    Kind m_kind;
    Params m_p;
    double m_rate = 48000.0, m_phase = 0.0;
    int m_channels = 0, m_length = 0, m_write = 0;
    std::array<std::vector<float>, kChannels> m_delay;
    float m_ap[kChannels][6]{};
    float m_feedback[kChannels]{};
    ParameterSmoother m_speed, m_depth, m_time, m_regen, m_mix, m_stereo;
};
} // namespace dreamdsp::dsp
