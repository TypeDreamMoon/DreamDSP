#include "StreamingSinc.h"
#include <algorithm>
#include <cmath>

namespace dreamdsp::dsp {
void StreamingSinc::prepare(double inputRate, double outputRate)
{
    constexpr double pi = 3.14159265358979323846;
    const double ratio = std::min(1.0, outputRate / inputRate);
    // A transition band is necessary: an ideal cutoff exactly at Nyquist
    // leaves frequencies immediately above it inadequately rejected.
    const double cutoff = 0.94 * ratio;
    m_delay = int(std::ceil(32.0 / ratio));
    m_taps = 2 * m_delay + 1;
    m_coefficients.resize(size_t((kPhases + 1) * m_taps));
    for (int phase = 0; phase <= kPhases; ++phase) {
        const double fraction = double(phase) / kPhases;
        float *row = m_coefficients.data() + phase * m_taps;
        double total = 0;
        for (int k = 0; k < m_taps; ++k) {
            const double x = double(m_delay - k) - fraction;
            const double arg = pi * cutoff * x;
            const double sinc = std::abs(arg) < 1e-12 ? 1 : std::sin(arg) / arg;
            const double w = std::abs(x) <= m_delay
                ? 0.42 + 0.5 * std::cos(pi * x / m_delay)
                       + 0.08 * std::cos(2 * pi * x / m_delay) : 0;
            const double h = cutoff * sinc * w;
            row[m_taps - 1 - k] = float(h);
            total += h;
        }
        for (int k = 0; k < m_taps; ++k) row[k] /= float(total);
    }
}

float StreamingSinc::read(const float *history, double time) const noexcept
{
    const double base = std::floor(time);
    const double position = (time - base) * kPhases;
    const int phase = std::min(int(position), kPhases - 1);
    const float blend = float(position - phase);
    const auto at = size_t(uint64_t(int64_t(base)) & (kRing - 1));
    const float *signal = history + at + kRing - (m_taps - 1);
    const float *a = m_coefficients.data() + phase * m_taps;
    const float *b = a + m_taps;
    float sum = 0;
    for (int k = 0; k < m_taps; ++k)
        sum += (a[k] + blend * (b[k] - a[k])) * signal[k];
    return sum;
}
} // namespace dreamdsp::dsp
