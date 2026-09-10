#include "LoudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace dreamdsp::dsp {

// ------------------------------------------------------------- KWeighting

void KWeighting::prepare(double sampleRate) noexcept
{
    const double fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    constexpr double kPi = 3.14159265358979323846;

    // Stage 1 -- the shelf. These four constants are BS.1770's, not a fit.
    {
        constexpr double f0 = 1681.974450955533;
        constexpr double G = 3.999843853973347;
        constexpr double Q = 0.7071752369554196;

        const double K = std::tan(kPi * f0 / fs);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        m_b0 = (Vh + Vb * K / Q + K * K) / a0;
        m_b1 = 2.0 * (K * K - Vh) / a0;
        m_b2 = (Vh - Vb * K / Q + K * K) / a0;
        m_a1 = 2.0 * (K * K - 1.0) / a0;
        m_a2 = (1.0 - K / Q + K * K) / a0;
    }

    // Stage 2 -- the 38 Hz highpass. Its numerator is (1, -2, 1) by definition,
    // so only the denominator is designed.
    {
        constexpr double f0 = 38.13547087602444;
        constexpr double Q = 0.5003270373238773;

        const double K = std::tan(kPi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        m_g1 = 2.0 * (K * K - 1.0) / a0;
        m_g2 = (1.0 - K / Q + K * K) / a0;
    }

    reset();
}

void KWeighting::reset() noexcept
{
    m_z1 = m_z2 = m_h1 = m_h2 = 0.0;
}

void KWeighting::coefficients(double *shelfB, double *shelfA, double *hpA) const noexcept
{
    shelfB[0] = m_b0; shelfB[1] = m_b1; shelfB[2] = m_b2;
    shelfA[0] = 1.0;  shelfA[1] = m_a1; shelfA[2] = m_a2;
    hpA[0] = 1.0;     hpA[1] = m_g1;    hpA[2] = m_g2;
}

// ----------------------------------------------------------- LoudnessMeter

double LoudnessMeter::weightFor(int channel, int channelCount) noexcept
{
    if (channelCount <= 2)
        return 1.0;
    // 5.1 and 7.1 in the order Windows uses: L R C LFE BL BR SL SR.
    if (channel == 3)
        return 0.0;                      // LFE is excluded by the standard
    return channel <= 2 ? 1.0 : 1.41;    // surrounds count 1.41
}

void LoudnessMeter::prepare(double sampleRate, int channels, double windowMs)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_channels = clampTo(channels, 1, kMaxChannels);
    m_blockLen = std::max(1, int(m_sampleRate * kBlockMs / 1000.0 + 0.5));
    m_blocks = std::max(1, int(windowMs / kBlockMs + 0.5));

    for (int c = 0; c < m_channels; ++c) {
        m_k[c].prepare(m_sampleRate);
        m_weight[c] = weightFor(c, m_channels);
    }
    m_ring.assign(size_t(m_blocks), 0.0);
    reset();
}

void LoudnessMeter::reset() noexcept
{
    for (int c = 0; c < m_channels; ++c)
        m_k[c].reset();
    std::fill(m_ring.begin(), m_ring.end(), 0.0);
    m_acc = 0.0;
    m_accCount = 0;
    m_head = 0;
    m_filled = 0;
    m_sum = 0.0;
    m_lufs = -200.0;
}

bool LoudnessMeter::process(const AudioBuffer &buf) noexcept
{
    if (!buf.valid() || m_ring.empty())
        return false;

    const int channels = std::min(buf.channelCount, m_channels);
    bool crossed = false;

    for (int i = 0; i < buf.frames; ++i) {
        double sum = 0.0;
        for (int c = 0; c < channels; ++c) {
            const double w = m_weight[c];
            if (w == 0.0) {
                // The LFE still has to run through the filter, or its state
                // would be stale the moment the weights change.
                m_k[c].process(double(buf.channels[c][i]));
                continue;
            }
            const double y = m_k[c].process(double(buf.channels[c][i]));
            sum += w * y * y;
        }
        m_acc += sum;

        if (++m_accCount >= m_blockLen) {
            const double meanSquare = m_acc / double(m_accCount);
            m_sum -= m_ring[size_t(m_head)];
            m_ring[size_t(m_head)] = meanSquare;
            m_sum += meanSquare;
            m_head = (m_head + 1) % m_blocks;
            if (m_filled < m_blocks)
                ++m_filled;

            // The running sum accumulates rounding over an unbounded number of
            // blocks. Every full turn of the ring, rebuild it from the ring
            // itself: bounded work, and it stops a long stream from drifting.
            if (m_head == 0) {
                double exact = 0.0;
                for (double v : m_ring)
                    exact += v;
                m_sum = exact;
            }

            const double mean = m_sum / double(m_filled);
            m_lufs = (m_filled >= m_blocks && mean > 0.0)
                         ? -0.691 + 10.0 * std::log10(mean)
                         : -200.0;
            m_acc = 0.0;
            m_accCount = 0;
            crossed = true;
        }
    }
    return crossed;
}

} // namespace dreamdsp::dsp
