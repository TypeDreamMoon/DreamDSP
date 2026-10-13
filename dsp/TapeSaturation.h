#pragma once

#include "DspTypes.h"
#include "Oversampler.h"
#include "ParameterSmoother.h"

#include <array>

namespace dreamdsp::dsp {

// Tape-flavoured algorithmic approximation, not a captured machine or a
// physical hysteresis solver. A bounded magnetic-memory state and slow drive
// sag soften transients, a broad 65 Hz head bump lifts lows, and a one-pole
// low-pass softens highs. Two existing Oversampler2x stages run it at 4x.
class TapeSaturation
{
public:
    struct Params {
        float driveDb = 6.0f;     // 0..24, gain into the saturating recording path
        float toneHz = 12000.0f;  // 2000..20000, wet-path low-pass corner
        float bumpDb = 1.5f;      // 0..6, small-signal lift at the fixed 65 Hz bump
        float memory = 0.35f;     // 0..1, magnetic-memory feedback and slow sag
        float mix = 1.0f;         // 0..1, blended INSIDE the same 4x filter path
        float outputDb = 0.0f;    // -24..6, output trim after dry/wet blending
    };

    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &params) noexcept;
    void process(const AudioBuffer &buffer) noexcept;
    bool supported() const noexcept { return m_supported; }
    // No explicit block/lookahead delay. IIR oversampling has frequency-
    // dependent phase; mix=0 is the aligned filtered dry, not raw bypass.
    int latencySamples() const noexcept { return 0; }

private:
    static constexpr int kMaxChannels = 8;
    struct Channel {
        Oversampler2x outer, inner;
        DcBlocker dc;
        float bandState = 0, lowState = 0;
        float flux = 0, envelope = 0, toneState = 0;
        void reset() noexcept;
    };
    Params m_p;
    std::array<Channel, kMaxChannels> m_channel;
    ParameterSmoother m_drive, m_tone, m_bump, m_memory, m_mix, m_output;
    double m_rate = 48000.0;
    int m_channels = 0;
    bool m_supported = false, m_started = false;
    float m_bumpG = 0, m_bumpDenom = 1;
    float m_fluxRate = 0, m_attackRate = 0, m_releaseRate = 0;
    float m_targetDrive = 1, m_targetTone = 0, m_targetBump = 1, m_targetOutput = 1;
};

static_assert(sizeof(TapeSaturation::Params) == 24, "Tape parameters are six wire floats");

} // namespace dreamdsp::dsp
