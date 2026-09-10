#include "EffectChain.h"

#include <cstring>

namespace dreamdsp::dsp {

void EffectChain::prepare(double sampleRate, int channels, int maxFrames)
{
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_maxFrames = maxFrames;

    m_eq.prepare(sampleRate, channels);
    m_graphic.prepare(sampleRate, channels);
    m_loudness.prepare(sampleRate, channels);
    m_dynBass.prepare(sampleRate, channels);
    m_bass.prepare(sampleRate, channels);
    m_exciter.prepare(sampleRate, channels);
    m_tube.prepare(sampleRate, channels);
    m_comp.prepare(sampleRate, channels);
    m_multi.prepare(sampleRate, channels, maxFrames);
    m_reverb.prepare(sampleRate);
    m_width.prepare(sampleRate);
    m_crossfeed.prepare(sampleRate);
    // 64 partitions is 340 ms at 48 kHz and 170 ms at 96 kHz, which covers
    // headphone and room correction. Longer reverb impulse responses need a
    // larger budget, granted by the caller.
    m_conv.prepare(sampleRate, channels, 64);
    m_matrix.prepare(sampleRate, channels, maxFrames);
    m_delay.prepare(sampleRate, channels, maxFrames);
    m_clipper.prepare(sampleRate, channels);
    m_limiter.prepare(sampleRate, channels, maxFrames);
    m_transient.prepare(sampleRate, channels);
    m_autoGain.prepare(sampleRate, channels, maxFrames);
    m_nightMode.prepare(sampleRate, channels);

    // Zeroed rather than value-initialised: the Params members carry default
    // member initialisers, and `m_applied = {}` would seed an always-on reverb.
    // Starting from all-zero is what makes a chain that has never been given
    // parameters transparent by construction.
    std::memset(&m_applied, 0, sizeof m_applied);
    std::memcpy(m_order, kDefaultOrder, sizeof m_order);

    reset();
}

void EffectChain::reset()
{
    m_eq.reset();
    m_graphic.reset();
    m_loudness.reset();
    m_dynBass.reset();
    m_bass.reset();
    m_exciter.reset();
    m_tube.reset();
    m_comp.reset();
    m_multi.reset();
    m_reverb.reset();
    m_width.reset();
    m_crossfeed.reset();
    m_conv.reset();
    m_matrix.reset();
    m_delay.reset();
    m_clipper.reset();
    m_limiter.reset();
    m_transient.reset();
    m_autoGain.reset();
    m_nightMode.reset();
}

void EffectChain::apply(ParamBlock p) noexcept
{
    // By value, so the rate-dependent clamps can be applied without touching the
    // caller's copy -- and so the block installed is exactly the block compared
    // against next time.
    clampForRate(p, m_sampleRate);

    // An effect that is off and was off is never redesigned. An effect being
    // turned on has to be, which is why `live` is the union rather than the new
    // mask alone.
    const uint32_t live = p.enableMask | m_applied.enableMask;
    const uint32_t turnedOn = p.enableMask & ~m_applied.enableMask;

    // m_applied.<field> is updated only when setParams actually ran, so an
    // effect enabled later still sees a memcmp difference and gets its
    // parameters installed rather than silently running stale ones.
#define DREAMDSP_STAGE(bit, field, obj)                                        \
    if ((live & (bit))                                                         \
        && std::memcmp(&p.field, &m_applied.field, sizeof p.field) != 0) {     \
        obj.setParams(p.field);                                                \
        m_applied.field = p.field;                                             \
    }                                                                          \
    if (turnedOn & (bit))                                                      \
        obj.reset();

    DREAMDSP_STAGE(kEnEqualizer, eq, m_eq)
    DREAMDSP_STAGE(kEnGraphic, graphic, m_graphic)
    DREAMDSP_STAGE(kEnLoudness, loudness, m_loudness)
    DREAMDSP_STAGE(kEnDynBass, dynBass, m_dynBass)
    DREAMDSP_STAGE(kEnBass, bass, m_bass)
    DREAMDSP_STAGE(kEnExciter, exciter, m_exciter)
    DREAMDSP_STAGE(kEnTube, tube, m_tube)
    DREAMDSP_STAGE(kEnComp, comp, m_comp)
    DREAMDSP_STAGE(kEnMultiband, multiband, m_multi)
    DREAMDSP_STAGE(kEnReverb, reverb, m_reverb)
    DREAMDSP_STAGE(kEnWidth, width, m_width)
    DREAMDSP_STAGE(kEnCrossfeed, crossfeed, m_crossfeed)
    DREAMDSP_STAGE(kEnMatrix, matrix, m_matrix)
    DREAMDSP_STAGE(kEnDelay, delay, m_delay)
    DREAMDSP_STAGE(kEnLimiter, limiter, m_limiter)
    DREAMDSP_STAGE(kEnTransient, transient, m_transient)
    DREAMDSP_STAGE(kEnClipper, clipper, m_clipper)
    DREAMDSP_STAGE(kEnAutoGain, autoGain, m_autoGain)
    DREAMDSP_STAGE(kEnNightMode, nightMode, m_nightMode)

#undef DREAMDSP_STAGE

    // Deliberately outside the macro, for two structural reasons. The macro
    // calls reset() on every 0 -> 1 enable transition, which here would be a
    // multi-megabyte memset on the audio thread; and there is nothing to clear
    // anyway -- the frequency-delay line holds input spectra and is independent
    // of the impulse response, so an enable wants a crossfade, not a state
    // clear. Clearing it would discard valid history and make the first
    // partitions after every enable wrong.
    if ((live & kEnConvolution)
        && std::memcmp(&p.convolution, &m_applied.convolution,
                       sizeof p.convolution) != 0) {
        m_conv.setParams(p.convolution);
        m_applied.convolution = p.convolution;
    }
    m_conv.setEnabled((p.enableMask & kEnConvolution) != 0u);

    // Resetting on the 0 -> 1 transition is what stops a ten-minute-old reverb
    // tail from bursting into the output the moment the effect is switched back
    // on. It costs one buffer fill per user toggle.
    // Already validated as a permutation by sanitise(); copied rather than
    // re-checked, because apply() runs on the audio thread and the check has a
    // place it belongs.
    std::memcpy(m_order, p.order, sizeof m_order);

    m_applied.enableMask = p.enableMask;
    m_applied.flags = p.flags;
    m_applied.generation = p.generation;
}

void EffectChain::process(const AudioBuffer &buf)
{
    const uint32_t mask = activeMask();
    if (mask == 0u || !buf.valid())
        return;

    // The order is the user's, not the code's.
    //
    // kDefaultOrder is what this used to be written out as, and the reasoning
    // behind it is still worth having: correction first, because everything
    // after it should act on a corrected signal rather than on the transducer's
    // errors; things that add content next, so the dynamics see them; dynamics
    // after tone, so the compressor reacts to the finished sound; then the
    // output stage, where routing decides which speaker a channel ends up in
    // and the impulse response corrects for that speaker, so it has to see the
    // final assignment; delay after that, because it is distance; and the
    // limiter last, because a ceiling that anything runs after is not a ceiling.
    //
    // Every one of those is a default rather than a constraint. The chain
    // editor can put them in any order, and the two places that used to
    // open-code this -- here and the offline renderer -- now both walk the
    // same array.
    for (int i = 0; i < kStageCount; ++i) {
        switch (m_order[i]) {
        case kStageEqualizer:   if (mask & kEnEqualizer)   m_eq.process(buf);        break;
        case kStageGraphic:     if (mask & kEnGraphic)     m_graphic.process(buf);   break;
        case kStageLoudness:    if (mask & kEnLoudness)    m_loudness.process(buf);  break;
        case kStageDynBass:     if (mask & kEnDynBass)     m_dynBass.process(buf);   break;
        case kStageBass:        if (mask & kEnBass)        m_bass.process(buf);      break;
        case kStageExciter:     if (mask & kEnExciter)     m_exciter.process(buf);   break;
        case kStageTube:        if (mask & kEnTube)        m_tube.process(buf);      break;
        case kStageComp:        if (mask & kEnComp)        m_comp.process(buf);      break;
        case kStageMultiband:   if (mask & kEnMultiband)   m_multi.process(buf);     break;
        case kStageReverb:      if (mask & kEnReverb)      m_reverb.process(buf);    break;
        case kStageWidth:       if (mask & kEnWidth)       m_width.process(buf);     break;
        case kStageCrossfeed:   if (mask & kEnCrossfeed)   m_crossfeed.process(buf); break;
        case kStageMatrix:      if (mask & kEnMatrix)      m_matrix.process(buf);    break;
        case kStageConvolution: if (mask & kEnConvolution) m_conv.process(buf);      break;
        case kStageDelay:       if (mask & kEnDelay)       m_delay.process(buf);     break;
        case kStageLimiter:     if (mask & kEnLimiter)     m_limiter.process(buf);   break;
        case kStageTransient:   if (mask & kEnTransient)   m_transient.process(buf); break;
        case kStageClipper:     if (mask & kEnClipper)     m_clipper.process(buf);   break;
        case kStageAutoGain:    if (mask & kEnAutoGain)    m_autoGain.process(buf);  break;
        case kStageNightMode:   if (mask & kEnNightMode)   m_nightMode.process(buf); break;
        default: break;
        }
    }
}

} // namespace dreamdsp::dsp
