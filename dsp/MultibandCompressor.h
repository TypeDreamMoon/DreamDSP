#pragma once

#include "BiquadFilter.h"
#include "Compressor.h"

#include <vector>

namespace dreamdsp::dsp {

// Three-band compressor on a Linkwitz-Riley tree.
//
// The reason to have one at all: a single wideband compressor lets a kick drum
// duck the vocal, because the detector cannot tell them apart. Splitting the
// spectrum first means the bass band's gain reduction never touches the top.
//
// The crossover is LR4, whose defining property is that its outputs sum flat.
// With every band bypassed the whole thing must therefore be transparent, and
// that is the test that matters -- a multiband processor which colours the
// signal when it is doing nothing is worse than not having one.
class MultibandCompressor
{
public:
    static constexpr int kBands = 3;

    struct Params {
        float lowCrossHz = 250.0f;
        float highCrossHz = 3000.0f;
        Compressor::Params band[kBands];
        float bandGainDb[kBands] = { 0.0f, 0.0f, 0.0f };
        bool bandEnabled[kBands] = { true, true, true };
    };

    // maxFrames is the largest block process() will ever be handed. The band
    // scratch is sized here and never again: process() runs on a real-time
    // thread, where a resize would be an allocation inside audiodg.
    void prepare(double sampleRate, int channels, int maxFrames);
    void reset();
    void setParams(const Params &p);
    const Params &params() const { return m_p; }

    void process(const AudioBuffer &buf);

    float gainReductionDb(int band) const;

private:
    static constexpr int kMaxChannels = 8;

    Params m_p;
    double m_sampleRate = 48000.0;
    int m_channels = 0;

    // First split low from the rest, then split that rest again.
    LinkwitzRiley4 m_splitLow[kMaxChannels];
    LinkwitzRiley4 m_splitHigh[kMaxChannels];

    // The low band skips the second crossover, so it needs the same allpass
    // delay the other two pick up there or the bands no longer sum flat.
    BiquadFilter m_lowAllpass1[kMaxChannels], m_lowAllpass2[kMaxChannels];

    Compressor m_comp[kBands];
    std::vector<float> m_scratch[kBands];
    int m_maxFrames = 0;

    // dbToLin is a std::pow. Evaluating it per band per sample per channel came
    // to 2.3 million pow calls a second at 96 kHz and 8 channels -- by a wide
    // margin the largest single cost in the chain, and all of it recomputing
    // three constants.
    float m_bandGainLin[kBands] = { 1.0f, 1.0f, 1.0f };
};

} // namespace dreamdsp::dsp
