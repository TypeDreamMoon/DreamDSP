#include "core/Fft.h"

#include <cmath>

namespace dreamdsp {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

Fft::Fft(int size)
    : m_size(size)
{
    if (m_size < 2 || (m_size & (m_size - 1)) != 0) {
        m_size = 0;
        return;
    }

    int bits = 0;
    while ((1 << bits) < m_size)
        ++bits;

    m_reverse.resize(m_size);
    for (int i = 0; i < m_size; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) {
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        }
        m_reverse[i] = r;
    }

    // Twiddles for the largest stage; smaller stages index this with a stride.
    m_twiddles.resize(m_size / 2);
    for (int i = 0; i < m_size / 2; ++i) {
        const float angle = -2.0f * kPi * float(i) / float(m_size);
        m_twiddles[i] = std::complex<float>(std::cos(angle), std::sin(angle));
    }

    m_window.resize(m_size);
    double sum = 0.0;
    for (int i = 0; i < m_size; ++i) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * double(kPi) * double(i) / double(m_size - 1)));
        m_window[i] = float(w);
        sum += w;
    }
    // Sum of the window, not half of it: a windowed sine of amplitude A peaks
    // at A*sum/2, and magnitudeSpectrum already applies the single-sided factor
    // of 2 -- halving here as well would read 6 dB hot.
    m_windowGain = float(sum);

    m_scratch.resize(m_size);
}

void Fft::forward(QVector<std::complex<float>> &data) const
{
    if (m_size == 0 || data.size() != m_size)
        return;

    for (int i = 0; i < m_size; ++i) {
        const int j = m_reverse[i];
        if (i < j)
            std::swap(data[i], data[j]);
    }

    for (int len = 2; len <= m_size; len <<= 1) {
        const int half = len / 2;
        const int stride = m_size / len;
        for (int base = 0; base < m_size; base += len) {
            for (int k = 0; k < half; ++k) {
                const std::complex<float> w = m_twiddles[k * stride];
                const std::complex<float> a = data[base + k];
                const std::complex<float> b = data[base + k + half] * w;
                data[base + k] = a + b;
                data[base + k + half] = a - b;
            }
        }
    }
}

void Fft::magnitudeSpectrum(const float *samples, QVector<float> *magnitudes) const
{
    if (m_size == 0 || !samples || !magnitudes)
        return;

    for (int i = 0; i < m_size; ++i)
        m_scratch[i] = std::complex<float>(samples[i] * m_window[i], 0.0f);

    forward(m_scratch);

    const int bins = m_size / 2 + 1;
    magnitudes->resize(bins);
    for (int i = 0; i < bins; ++i) {
        // Single-sided: everything except DC and Nyquist appears twice.
        const float scale = (i == 0 || i == m_size / 2) ? 1.0f : 2.0f;
        (*magnitudes)[i] = std::abs(m_scratch[i]) * scale / m_windowGain;
    }
}

} // namespace dreamdsp
