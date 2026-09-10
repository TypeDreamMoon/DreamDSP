#pragma once

#include "DspTypes.h"

#include <vector>

namespace dreamdsp::dsp {

// ITU-R BS.1770 loudness measurement: the K-weighting filter, and a block
// meter built on it.
//
// This is measurement, not processing -- nothing here touches the audio. Two
// stages need to know how loud the programme is (the auto-volume leveller and
// the night-mode compressor) and both need the same answer, so the answer is
// computed in one place.
//
// Everything runs in double. That is not caution: the second K-weighting
// section is a 38 Hz highpass, and at 192 kHz its pole sits at 0.9987 with a
// float32 coefficient quantisation of 6e-8 -- enough to move the corner
// frequency by a measurable fraction and to put the filter's own noise floor
// at -82 dBFS. A meter whose noise floor is -82 dBFS cannot honestly report a
// -70 LUFS gate.

// The two biquads of BS.1770-5, designed rather than tabulated.
//
// The published coefficient tables cover 48 kHz only. Six lines of design code
// cover every rate, and reproduce Table 1 and Table 2 exactly at 48 kHz -- the
// self-test asserts that. The constants below are the ones in the standard's
// own design description, to full precision.
class KWeighting
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    inline double process(double x) noexcept
    {
        // Stage 1: high-frequency shelf, the "head" of the weighting.
        const double y1 = m_b0 * x + m_z1;
        m_z1 = m_b1 * x - m_a1 * y1 + m_z2;
        m_z2 = m_b2 * x - m_a2 * y1;

        // Stage 2: 38 Hz highpass. Its numerator is exactly (1, -2, 1), so the
        // multiplies fold away.
        const double y2 = y1 + m_h1;
        m_h1 = -2.0 * y1 - m_g1 * y2 + m_h2;
        m_h2 = y1 - m_g2 * y2;
        return y2;
    }

    // Exposed so the self-test can compare against the published tables.
    void coefficients(double *shelfB, double *shelfA, double *hpA) const noexcept;

private:
    // Transposed direct form II, which is the numerically better-behaved form
    // for a fixed-coefficient biquad and the one every other filter here uses.
    double m_b0 = 1.0, m_b1 = 0.0, m_b2 = 0.0, m_a1 = 0.0, m_a2 = 0.0;
    double m_g1 = 0.0, m_g2 = 0.0;
    double m_z1 = 0.0, m_z2 = 0.0, m_h1 = 0.0, m_h2 = 0.0;
};

// ---------------------------------------------------------------------------

// Momentary / short-term loudness over a sliding window of 100 ms blocks.
//
// BS.1770 gates in 400 ms blocks advancing every 100 ms. Rather than keep 400 ms
// of audio, this keeps one running sum of the per-block mean squares -- exact,
// O(1) per sample, and the memory is a ring of at most 30 doubles per channel
// group. `windowMs` picks momentary (400) or short-term (3000).
//
// What this deliberately does NOT implement is the integrated measurement: the
// two-pass relative gate and the histogram are for analysing a finished file,
// and a stage that has to decide what gain to apply in the next 100 ms cannot
// wait for the end of the programme.
class LoudnessMeter
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr double kBlockMs = 100.0;

    void prepare(double sampleRate, int channels, double windowMs);
    void reset() noexcept;

    // Reads the buffer without modifying it. Returns true when a new block
    // boundary was crossed, i.e. when lufs() has a fresh value.
    bool process(const AudioBuffer &buf) noexcept;

    // LUFS, or -200.0 when the window has not filled yet or holds silence.
    // BS.1770's own -0.691 calibration offset is applied.
    double lufs() const noexcept { return m_lufs; }

    // How many blocks are still needed before lufs() means anything.
    bool ready() const noexcept { return m_filled >= m_blocks; }

private:
    // BS.1770 channel weights. L, R and C count 1.0; the surrounds count 1.41;
    // LFE is excluded entirely. Index is the WAVE_FORMAT_EXTENSIBLE channel
    // order Windows hands an APO, so channel 3 of a 5.1 or 7.1 stream is the
    // LFE and weighs nothing. A 1- or 2-channel stream has no LFE, so the
    // weights are all 1.0 -- which is why the table is chosen by channel count.
    static double weightFor(int channel, int channelCount) noexcept;

    double m_sampleRate = 48000.0;
    int m_channels = 0;
    int m_blockLen = 4800;     // samples per 100 ms block
    int m_blocks = 4;          // blocks in the window (4 = 400 ms, 30 = 3 s)

    KWeighting m_k[kMaxChannels];
    double m_weight[kMaxChannels] = {};

    double m_acc = 0.0;        // weighted sum of squares in the block being filled
    int m_accCount = 0;

    std::vector<double> m_ring;   // per-block mean square
    int m_head = 0;
    int m_filled = 0;
    double m_sum = 0.0;           // running sum of m_ring

    double m_lufs = -200.0;
};

} // namespace dreamdsp::dsp
