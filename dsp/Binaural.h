#pragma once

#include "DspTypes.h"

#include <array>
#include <vector>

namespace dreamdsp::dsp {

// Two virtual loudspeakers around a listener facing the screen. Model 1 uses
// MIT KEMAR measurements with azimuth/elevation cues; model 0 retains the old
// horizontal analytic sphere. Neither model is personalised.
// All direction filters are built in prepare(); process only interpolates the
// immutable table. Each input reaches BOTH ears (a genuine 2 x 2 renderer).
// After prepare(), setters, reset and processing belong to the audio thread;
// the host must deliver cross-thread parameters through its safe snapshot API.
class Binaural
{
public:
    struct Params {
        float speakerAngleDeg = 30.0f; // symmetric speakers, 0 .. 90 degrees
        float yawDeg = 0.0f;           // clockwise speaker-ring rotation, -180 .. 180
        float mix = 1.0f;
        float outputDb = -6.0f;        // summation headroom, -24 .. 6 dB
        float elevationDeg = 0.0f;     // physical elevation, -40 .. 90 degrees
        uint32_t model = 1;            // 0: legacy sphere; 1: measured MIT KEMAR
    };

    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &p) noexcept;
    void process(const AudioBuffer &buf) noexcept;
    bool supported() const noexcept { return m_supported; }
    // No extra block/alignment buffering. This does NOT mean the spatial
    // impulse response has zero delay: see acousticDelaySamples(). The dry
    // path stays direct, as in a reverb; partial mixes colour the phase.
    int latencySamples() const noexcept { return 0; }
    // Common causal padding for the selected model (target while switching).
    // Model 1 additionally retains all native HRIR phase/delay and ITD; this
    // value is not the total acoustic arrival time or an aligned bypass delay.
    int acousticDelaySamples() const noexcept
    { return m_supported ? (m_p.model == 0 ? m_latency : m_measuredLatency) : 0; }
    int filterLength() const noexcept { return m_p.model == 0 ? m_analyticTaps : m_measuredTaps; }
    static constexpr const char *modelName(uint32_t model = 1) noexcept
    { return model == 0 ? "Analytic spherical head" : "MIT KEMAR diffuse-field (Gardner / Martin)"; }

private:
    static constexpr int kDirections = 360;
    static constexpr int kMaxTaps = 1024;
    Params m_p;
    std::vector<float> m_filters;
    std::vector<float> m_measured;
    std::array<std::array<float, 2 * kMaxTaps>, 2> m_history{};
    int m_taps = 0;
    int m_analyticTaps = 0, m_measuredTaps = 0, m_measuredLatency = 0;
    int m_latency = 0;
    int m_write = 0;
    bool m_supported = false;
    bool m_hasProcessed = false;
    float m_angle = 30.0f, m_yaw = 0.0f, m_elevation = 0.0f, m_mix = 1.0f;
    float m_modelBlend = 1.0f, m_modelStep = 0.0f;
    float m_gain = 0.5011872f, m_targetGain = 0.5011872f;
    float m_directionRate = 0.0f, m_levelRate = 0.0f;
    void prepareMeasured(double sampleRate);
    const float *measuredFilter(int ring, int index, int ear) const noexcept;
};

static_assert(sizeof(Binaural::Params) == 24, "Binaural parameters are wire scalars");
static_assert(offsetof(Binaural::Params, elevationDeg) == 16, "Keep the legacy wire prefix");

} // namespace dreamdsp::dsp
