#pragma once
#include <array>
#include <vector>
#include "DspTypes.h"
#include "ParameterSmoother.h"

namespace dreamdsp::dsp {
// Eight delay lines and an orthogonal Householder feedback matrix.
class FdnReverb
{
public:
    struct Params {
        float decaySeconds = 1.8f;
        float dampingHz = 6000.0f;
        float preDelayMs = 15.0f;
        float mix = 0.25f;
        float width = 1.0f;
        float outputDb = 0.0f;
    };
    // Invalid/out-of-range rates fall back to 48 kHz; supported range 8--384 kHz.
    void prepare(double rate, int channels);
    void reset() noexcept;
    // Non-finite direct parameters are replaced with bounded defaults.
    // outputDb trims the complete dry/wet mix, including mix == 0.
    void setParams(const Params &p) noexcept;
    void process(const AudioBuffer &buf) noexcept;
private:
    double m_rate = 48000.0;
    Params m_p;
    std::array<std::vector<float>, 8> m_lines;
    std::vector<float> m_pre;
    int m_pos[8]{}, m_prePos = 0;
    float m_low[8]{};
    ParameterSmoother m_gain[8], m_damp, m_preTime, m_mix, m_width, m_output;
};
} // namespace dreamdsp::dsp
