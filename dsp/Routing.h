#pragma once

#include "DspTypes.h"

#include <cstdint>
#include <vector>

// Channel routing and per-channel delay: what Equalizer APO spells `Copy:` and
// `Delay:`.
//
// These are the last two things in the chain because they describe the physical
// output rather than the sound -- which speaker each stream channel ends up in,
// and how far away it is. Everything above them is tone; these two are wiring.

namespace dreamdsp::dsp {

// An N x N mixing matrix, out = M * in.
//
// APO writes this as `Copy: L=0.5*L+0.5*R`, one assignment per output. A matrix
// is the same thing with the parser removed: every assignment it can express is
// a row, and a row is what actually gets executed.
//
// Not in place. out[0] depends on every input, so the inputs have to survive
// until the last output is written -- hence the scratch buffer, sized once in
// prepare().
class ChannelMatrix
{
public:
    static constexpr int kMaxChannels = 8;

    struct Params {
        // gain[out][in]. Identity is the neutral setting, NOT all-zero: an
        // all-zero matrix is silence. That is why this stage, unlike most of
        // the others, is meaningless without its enable bit -- and why silence
        // is the failure it degrades to, which is the safe direction.
        float gain[kMaxChannels][kMaxChannels];
    };

    void prepare(double sampleRate, int channels, int maxFrames);
    void reset() noexcept {}
    void setParams(const Params &p) noexcept;
    void process(const AudioBuffer &buf);

    // The identity matrix, i.e. what "no routing" means.
    static Params identity() noexcept;
    // True when `p` is the identity to within float exactness, so the caller
    // can skip the stage entirely rather than multiplying by 1 eight times.
    static bool isIdentity(const Params &p) noexcept;

private:
    Params m_p{};
    bool m_identity = true;
    int m_channels = 0;
    std::vector<float> m_scratch;   // channels * maxFrames
};

// ---------------------------------------------------------------------------

// Per-channel delay, with sub-sample resolution.
//
// Equalizer APO's `Delay:` rounds to whole samples -- at 48 kHz that quantises
// speaker alignment to 7.1 mm of path length, which is the same order as the
// distances people are trying to correct. A four-tap Lagrange interpolator
// costs three extra multiplies per sample and removes the quantisation.
//
// The smallest non-zero delay is one sample, because the interpolator reads one
// sample ahead of the integer tap. At 48 kHz that is 0.02 ms, which is below
// anything the parameter is used to express.
class ChannelDelay
{
public:
    static constexpr int kMaxChannels = 8;
    // 100 ms is 34 m of path length -- far past speaker alignment, and short
    // enough that the line costs 307 kB at 96 kHz rather than megabytes.
    static constexpr float kMaxMs = 100.0f;

    struct Params {
        float ms[kMaxChannels];
    };

    void prepare(double sampleRate, int channels, int maxFrames);
    void reset() noexcept;
    void setParams(const Params &p) noexcept;
    void process(const AudioBuffer &buf);

    // Largest configured delay, in samples. What the APO has to report to
    // Windows as its latency.
    int latencySamples() const noexcept { return m_maxDelaySamples; }

private:
    Params m_p{};
    double m_sampleRate = 48000.0;
    int m_channels = 0;
    int m_lineLength = 0;
    int m_write = 0;
    int m_maxDelaySamples = 0;

    // Per channel: the integer tap and the four interpolation coefficients.
    int m_intDelay[kMaxChannels]{};
    float m_coeff[kMaxChannels][4]{};
    bool m_active[kMaxChannels]{};

    std::vector<float> m_line;   // channels * lineLength, contiguous
};

} // namespace dreamdsp::dsp
