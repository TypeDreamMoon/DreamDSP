#pragma once

#include "BiquadFilter.h"

#include <array>

namespace dreamdsp::dsp {

// An eight-unit leaky recurrent (echo-state) network with a trained readout.
// The bundled model was trained on an original synthetic dynamic saturator,
// not on captured hardware. See scripts/train-neural-saturation.py and the
// model manifest for reproducible training and held-out error measurements.
// State, parameters and faultCount() are owned by the audio thread. Publish
// fault telemetry through the host's snapshot mechanism, not a racing getter.
class NeuralSaturation
{
public:
    struct Params {
        float driveDb = 0.0f;   // -12 .. 18 dB; model input is bounded to +/-2
        float mix = 1.0f;
        float outputDb = 0.0f;  // -24 .. 6 dB
        uint32_t model = 0;     // 0 = Synthetic Memory Drive v1
    };

    static constexpr double kModelSampleRate = 48000.0;
    static constexpr const char *modelName() noexcept { return "Synthetic Memory Drive"; }
    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &p) noexcept;
    void process(const AudioBuffer &buf) noexcept;
    bool supported() const noexcept { return m_supported; }
    int latencySamples() const noexcept { return 0; } // causal, no lookahead/SRC
    uint32_t faultCount() const noexcept { return m_faultCount; }

private:
    static constexpr int kUnits = 8;
    static constexpr int kMaxChannels = 8;
    struct Channel {
        std::array<float, kUnits> hidden{};
        DcBlocker dc;
    };
    float forward(float x, Channel &channel) noexcept;
    void fault(Channel &channel) noexcept;
    Params m_p;
    std::array<Channel, kMaxChannels> m_state{};
    int m_channels = 0;
    bool m_supported = false;
    bool m_hasProcessed = false;
    uint32_t m_faultCount = 0;
    float m_rate = 0.0f;
    float m_drive = 1.0f, m_mix = 1.0f, m_gain = 1.0f;
    float m_targetDrive = 1.0f, m_targetGain = 1.0f;
};

static_assert(sizeof(NeuralSaturation::Params) == 16, "Neural parameters are wire scalars");

} // namespace dreamdsp::dsp
