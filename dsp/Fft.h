#pragma once

#include <complex>
#include <vector>

namespace dreamdsp::dsp {

// Radix-2 Cooley-Tukey, with the real-input optimisation that convolution
// needs.
//
// Deliberately not a dependency. Equalizer APO ships fftw3f.dll and KissFFT is
// the usual vendored choice, but pulling either into audiodg.exe means another
// DLL that has to load inside a system process, or another licence to honour,
// for a transform this small.
//
// Everything is sized in the constructor. forward()/inverse()/realForward()/
// realInverse() allocate nothing, so they are safe on the audio callback.
//
// Convention: the forward transform is unnormalised and the inverse divides by
// N, so inverse(forward(x)) == x. That is what makes
// realInverse(realForward(a) * realForward(b)) a plain circular convolution
// with no stray scale factor.
class Fft
{
public:
    Fft() = default;
    // size must be a power of two and at least 2. Anything else leaves the
    // object inert, with size() == 0.
    explicit Fft(int size) { prepare(size); }

    void prepare(int size);

    int size() const { return m_size; }
    bool valid() const { return m_size > 0; }

    // In-place complex transforms over exactly size() elements.
    void forward(std::complex<float> *data) const;
    void inverse(std::complex<float> *data) const;

    // --- real-input transforms ------------------------------------------
    //
    // These operate on 2*size() real samples using one size()-point complex
    // transform, which is the whole point: a real signal's spectrum is
    // conjugate-symmetric, so half the complex transform is redundant work.
    //
    // realSize() real samples <-> realBins() complex bins.
    int realSize() const { return m_size * 2; }
    int realBins() const { return m_size + 1; }

    // `in` holds realSize() samples, `out` receives realBins() bins.
    void realForward(const float *in, std::complex<float> *out) const;
    // `in` holds realBins() bins, `out` receives realSize() samples.
    // `in` is not modified.
    void realInverse(const std::complex<float> *in, float *out) const;

private:
    int m_size = 0;
    std::vector<int> m_reverse;                    // bit-reversal permutation
    std::vector<std::complex<float>> m_twiddles;   // for the largest stage
    std::vector<std::complex<float>> m_realTwiddles;

    // Scratch for the real transforms. Mutable because the transforms are
    // logically const and this is sized once, never grown.
    mutable std::vector<std::complex<float>> m_scratch;
};

} // namespace dreamdsp::dsp
