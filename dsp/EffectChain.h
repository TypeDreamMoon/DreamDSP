#pragma once

#include "Compressor.h"
#include "MultibandCompressor.h"
#include "ParamBlock.h"
#include "Reverb.h"
#include "Saturation.h"
#include "Stereo.h"

namespace dreamdsp::dsp {

// The whole effect rack, in one place.
//
// This exists so that there is exactly one definition of what DreamDSP does to
// audio. The copy running inside audiodg.exe and the offline renderer that
// produces the preview both call it, in the same order, from the same struct --
// so a preview that sounds right and a system output that does not cannot
// happen through divergence.
//
// prepare() is the only entry point that allocates. apply() and process() are
// real-time safe: no allocation, no lock, no syscall, no throw.
class EffectChain
{
public:
    // Not real-time. Sizes every buffer for the worst case the caller will hand
    // to process(), and resets all state.
    void prepare(double sampleRate, int channels, int maxFrames);

    // Not real-time in the strict sense (it clears delay lines), but allocation
    // free -- safe from Reset().
    void reset();

    // Installs a parameter block. Real-time safe.
    //
    // Cost on an unchanged block is eight memcmps totalling 232 bytes. Filter
    // design only runs for effects that are enabled *and* whose parameters
    // actually differ, so dragging one slider never redesigns the other seven.
    void apply(ParamBlock p) noexcept;

    // Runs the enabled effects in chain order. Real-time safe.
    void process(const AudioBuffer &buf);

    // What is actually running. Zero means the chain is a no-op and the caller
    // should take its pass-through path rather than calling process() at all.
    uint32_t appliedMask() const noexcept { return m_applied.enableMask; }
    uint32_t appliedGeneration() const noexcept { return m_applied.generation; }

    double sampleRate() const noexcept { return m_sampleRate; }
    int channels() const noexcept { return m_channels; }

private:
    double m_sampleRate = 0.0;
    int m_channels = 0;
    int m_maxFrames = 0;

    // The last block installed. Compared field by field against each incoming
    // block to decide what needs redesigning.
    ParamBlock m_applied{};

    VirtualBass m_bass;
    Exciter m_exciter;
    TubeStage m_tube;
    Compressor m_comp;
    MultibandCompressor m_multi;
    Reverb m_reverb;
    StereoWidener m_width;
    Crossfeed m_crossfeed;
};

} // namespace dreamdsp::dsp
