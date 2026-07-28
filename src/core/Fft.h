#pragma once

#include <QVector>

#include <complex>

namespace dreamdsp {

// A small iterative radix-2 Cooley-Tukey FFT.
//
// Deliberately not a dependency: Equalizer APO ships fftw3f.dll and KissFFT is
// a common vendored choice, but a 2048-point transform thirty times a second is
// far too cheap to justify either.
class Fft
{
public:
    // size must be a power of two.
    explicit Fft(int size);

    int size() const { return m_size; }

    // In-place forward transform.
    void forward(QVector<std::complex<float>> &data) const;

    // Hann window, precomputed for `size` samples. Applied by magnitudeSpectrum.
    const QVector<float> &window() const { return m_window; }

    // Real input (already `size` samples) -> magnitude of bins 0..size/2,
    // windowed and normalised so a full-scale sine reads 1.0 in its bin.
    void magnitudeSpectrum(const float *samples, QVector<float> *magnitudes) const;

private:
    int m_size = 0;
    QVector<int> m_reverse;                       // bit-reversal permutation
    QVector<std::complex<float>> m_twiddles;
    QVector<float> m_window;
    float m_windowGain = 1.0f;

    mutable QVector<std::complex<float>> m_scratch;
};

} // namespace dreamdsp
