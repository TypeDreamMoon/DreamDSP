#pragma once
#include "DspTypes.h"

namespace dreamdsp::dsp {
class ParameterSmoother
{
public:
    void prepare(double rate, float ms = 20.0f) noexcept { m_a = timeConstant(ms, rate); }
    void reset(float value) noexcept { m_value = m_target = value; }
    void setTarget(float value) noexcept { m_target = value; }
    float next() noexcept { return m_value = m_target + m_a * (m_value - m_target); }
private:
    float m_a = 0.0f, m_value = 0.0f, m_target = 0.0f;
};
} // namespace dreamdsp::dsp
