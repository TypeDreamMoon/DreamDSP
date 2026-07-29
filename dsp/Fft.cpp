#include "Fft.h"

#include <cmath>

namespace dreamdsp::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void Fft::prepare(int size)
{
    m_size = (size >= 2 && (size & (size - 1)) == 0) ? size : 0;
    if (m_size == 0)
        return;

    int bits = 0;
    while ((1 << bits) < m_size)
        ++bits;

    m_reverse.assign(size_t(m_size), 0);
    for (int i = 0; i < m_size; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) {
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        }
        m_reverse[size_t(i)] = r;
    }

    // Twiddles for the largest stage; smaller stages index this with a stride.
    m_twiddles.assign(size_t(m_size / 2), {});
    for (int i = 0; i < m_size / 2; ++i) {
        const double angle = -2.0 * kPi * double(i) / double(m_size);
        m_twiddles[size_t(i)] = { float(std::cos(angle)), float(std::sin(angle)) };
    }

    // Twiddles for the real-transform unpacking step, which works over the
    // full 2N-point grid rather than the N-point one above.
    m_realTwiddles.assign(size_t(m_size + 1), {});
    for (int i = 0; i <= m_size; ++i) {
        const double angle = -kPi * double(i) / double(m_size);
        m_realTwiddles[size_t(i)] = { float(std::cos(angle)), float(std::sin(angle)) };
    }

    m_scratch.assign(size_t(m_size), {});
}

void Fft::forward(std::complex<float> *data) const
{
    if (m_size == 0 || !data)
        return;

    for (int i = 0; i < m_size; ++i) {
        const int j = m_reverse[size_t(i)];
        if (i < j)
            std::swap(data[i], data[j]);
    }

    for (int len = 2; len <= m_size; len <<= 1) {
        const int half = len / 2;
        const int stride = m_size / len;
        for (int base = 0; base < m_size; base += len) {
            for (int k = 0; k < half; ++k) {
                const std::complex<float> w = m_twiddles[size_t(k * stride)];
                const std::complex<float> a = data[base + k];
                const std::complex<float> b = data[base + k + half] * w;
                data[base + k] = a + b;
                data[base + k + half] = a - b;
            }
        }
    }
}

void Fft::inverse(std::complex<float> *data) const
{
    if (m_size == 0 || !data)
        return;

    // The conjugate trick: conj(FFT(conj(x))) / N. One transform, no second
    // twiddle table, and no chance of the two drifting out of step.
    for (int i = 0; i < m_size; ++i)
        data[i] = std::conj(data[i]);

    forward(data);

    const float scale = 1.0f / float(m_size);
    for (int i = 0; i < m_size; ++i)
        data[i] = std::conj(data[i]) * scale;
}

void Fft::realForward(const float *in, std::complex<float> *out) const
{
    if (m_size == 0 || !in || !out)
        return;

    // Pack 2N real samples as N complex ones: even samples into the real part,
    // odd into the imaginary. One N-point complex transform then carries all
    // the information of a 2N-point real one.
    for (int i = 0; i < m_size; ++i)
        m_scratch[size_t(i)] = { in[2 * i], in[2 * i + 1] };

    forward(m_scratch.data());

    // Unpack. Z[k] holds the transform of the interleaved sequence; splitting
    // it into its conjugate-even and conjugate-odd parts recovers the
    // transforms of the even and odd subsequences, which then combine with a
    // half-grid twiddle.
    for (int k = 0; k <= m_size; ++k) {
        const std::complex<float> zk = m_scratch[size_t(k % m_size)];
        const std::complex<float> zn = std::conj(m_scratch[size_t((m_size - k) % m_size)]);

        const std::complex<float> even = (zk + zn) * 0.5f;
        const std::complex<float> odd = (zk - zn) * std::complex<float>(0.0f, -0.5f);

        out[k] = even + m_realTwiddles[size_t(k)] * odd;
    }
}

void Fft::realInverse(const std::complex<float> *in, float *out) const
{
    if (m_size == 0 || !in || !out)
        return;

    // Exactly the inverse of the packing above.
    for (int k = 0; k < m_size; ++k) {
        const std::complex<float> xk = in[k];
        const std::complex<float> xn = std::conj(in[m_size - k]);

        const std::complex<float> even = (xk + xn) * 0.5f;
        // The forward step multiplied `odd` by exp(-i*pi*k/N); undo it before
        // recombining, which is a multiply by the conjugate.
        const std::complex<float> odd =
            (xk - xn) * 0.5f * std::conj(m_realTwiddles[size_t(k)]);

        m_scratch[size_t(k)] = even + std::complex<float>(0.0f, 1.0f) * odd;
    }

    inverse(m_scratch.data());

    for (int i = 0; i < m_size; ++i) {
        out[2 * i] = m_scratch[size_t(i)].real();
        out[2 * i + 1] = m_scratch[size_t(i)].imag();
    }
}

} // namespace dreamdsp::dsp
