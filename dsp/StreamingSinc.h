#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dreamdsp::dsp {

// Causal, band-limited interpolation over a caller-owned duplicated ring.
// prepare() owns all allocation/trigonometry. read() only interpolates two
// prepared phase rows and performs a bounded dot product, independent of blocks.
// Internal adapter: prepare rates must be finite, within 8-192 kHz, with one
// endpoint at 48 kHz. NeuralSaturation validates these preconditions.
class StreamingSinc {
public:
    static constexpr int kRing = 1024; // power of two; > longest supported kernel
    void prepare(double inputRate, double outputRate);
    int delay() const noexcept { return m_delay; } // input samples
    float read(const float *history, double time) const noexcept;
    static void write(float *history, int64_t index, float value) noexcept
    {
        const auto at = size_t(uint64_t(index) & (kRing - 1));
        history[at] = history[at + kRing] = value;
    }
private:
    static constexpr int kPhases = 512;
    int m_delay = 0, m_taps = 0;
    std::vector<float> m_coefficients;
};
} // namespace dreamdsp::dsp
