#pragma once

#include "DspTypes.h"
#include "Oversampler.h"

#include <cstdint>
#include <vector>

namespace dreamdsp::dsp {

// A look-ahead peak limiter, and the output gain that feeds it.
//
// The equalizer's rail is a safety device: it stops a hostile parameter file
// from becoming full-scale noise, and it does that by clipping, which is
// distortion. This is the musical version -- it brings the level down before
// the peak arrives, so nothing is clipped at all.
//
// The threshold is a guarantee, not a target. The gain finally applied to each
// sample is min(smoothed gain, threshold / |x|), so no output sample can exceed
// the threshold whatever the envelope is doing. The smoothing is what makes it
// sound like a limiter rather than a clipper; the final min is what makes the
// number on the control true.
class Limiter
{
public:
    static constexpr int kMaxChannels = 8;
    // 10 ms of look-ahead is far more than a peak limiter needs and bounds the
    // delay line; the default is 1.5 ms.
    static constexpr float kMaxLookaheadMs = 10.0f;

    struct Params {
        // Applied before the limiter, not after, so the threshold is the real
        // ceiling. "Turn it up until it starts limiting" is then a meaningful
        // instruction, which it would not be if the gain came afterwards.
        float gainDb = 0.0f;
        float thresholdDb = -0.3f;
        float releaseMs = 100.0f;
        float lookaheadMs = 1.5f;
        // Measure the peak of the reconstructed waveform, not of the samples.
        //
        // Without this the guarantee above is true but narrower than it sounds:
        // it bounds the *sample* values, and a DAC's reconstruction filter
        // draws a continuous curve between them that can overshoot every one.
        // Measured on this limiter, the overshoot reaches +1.28 dBTP -- so a
        // ceiling set to -0.3 dBFS could still present nearly +1 dBFS to the
        // converter. ITU-R BS.1770 defines true peak by oversampling, and this
        // uses the 4x the standard specifies.
        bool truePeak = true;
        uint8_t pad[3] = {};
    };

    void prepare(double sampleRate, int channels, int maxFrames);
    void reset() noexcept;
    void setParams(const Params &p) noexcept;
    void process(const AudioBuffer &buf);

    // The look-ahead, which the stream is genuinely held back by.
    int latencySamples() const noexcept { return m_lookahead; }
    // Current gain reduction in dB, for a meter. Never positive.
    double reductionDb() const noexcept;

private:
    // Sliding minimum of the per-sample target gain over the look-ahead window.
    //
    // A monotonic deque rather than a rescan: the window is 144 samples at
    // 96 kHz, and rescanning it per sample would make the stage cost more than
    // the rest of the chain put together. Amortised O(1), and the memory is a
    // fixed ring sized in prepare().
    struct SlidingMin {
        std::vector<float> value;
        std::vector<int> index;
        int head = 0, tail = 0;      // [head, tail)
        int n = 0;                   // samples pushed so far

        void prepare(int capacity);
        void reset() noexcept;
        // Pushes v, drops anything older than `window`, returns the window min.
        float push(float v, int window) noexcept;
    };

    Params m_p;
    double m_sampleRate = 48000.0;
    int m_channels = 0;
    int m_lookahead = 0;
    int m_lineLength = 0;
    int m_write = 0;

    float m_preGain = 1.0f;
    float m_threshold = 1.0f;
    float m_releaseCoef = 0.0f;
    float m_gain = 1.0f;

    SlidingMin m_min;
    std::vector<float> m_line;   // channels * lineLength
    // One linked peak per frame, delayed alongside the audio, so the final
    // clamp can be checked against the same number the gain was computed from.
    std::vector<float> m_peakLine;
    // 4x, as two nested 2x stages. Only the interpolation half runs.
    Oversampler2x m_tp1[kMaxChannels], m_tp2[kMaxChannels];
};

} // namespace dreamdsp::dsp
