#include "EffectChain.h"

#include <cstring>

namespace dreamdsp::dsp {

void EffectChain::prepare(double sampleRate, int channels, int maxFrames)
{
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_maxFrames = maxFrames;

    m_bass.prepare(sampleRate, channels);
    m_exciter.prepare(sampleRate, channels);
    m_tube.prepare(sampleRate, channels);
    m_comp.prepare(sampleRate, channels);
    m_multi.prepare(sampleRate, channels, maxFrames);
    m_reverb.prepare(sampleRate);
    m_width.prepare(sampleRate);
    m_crossfeed.prepare(sampleRate);

    // Zeroed rather than value-initialised: the Params members carry default
    // member initialisers, and `m_applied = {}` would seed an always-on reverb.
    // Starting from all-zero is what makes a chain that has never been given
    // parameters transparent by construction.
    std::memset(&m_applied, 0, sizeof m_applied);

    reset();
}

void EffectChain::reset()
{
    m_bass.reset();
    m_exciter.reset();
    m_tube.reset();
    m_comp.reset();
    m_multi.reset();
    m_reverb.reset();
    m_width.reset();
    m_crossfeed.reset();
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

    DREAMDSP_STAGE(kEnBass, bass, m_bass)
    DREAMDSP_STAGE(kEnExciter, exciter, m_exciter)
    DREAMDSP_STAGE(kEnTube, tube, m_tube)
    DREAMDSP_STAGE(kEnComp, comp, m_comp)
    DREAMDSP_STAGE(kEnMultiband, multiband, m_multi)
    DREAMDSP_STAGE(kEnReverb, reverb, m_reverb)
    DREAMDSP_STAGE(kEnWidth, width, m_width)
    DREAMDSP_STAGE(kEnCrossfeed, crossfeed, m_crossfeed)

#undef DREAMDSP_STAGE

    // Resetting on the 0 -> 1 transition is what stops a ten-minute-old reverb
    // tail from bursting into the output the moment the effect is switched back
    // on. It costs one buffer fill per user toggle.
    m_applied.enableMask = p.enableMask;
    m_applied.generation = p.generation;
}

void EffectChain::process(const AudioBuffer &buf)
{
    const uint32_t mask = m_applied.enableMask;
    if (mask == 0u || !buf.valid())
        return;

    // Chain order, and why:
    //   bass and exciter first -- they add content the later stages should see
    //   tube next, colouring the whole spectrum
    //   dynamics after tone, so the compressor reacts to the finished sound
    //   reverb after dynamics, because compressing a tail pumps it
    //   width and crossfeed last: they are output-stage, not part of the tone
    //
    // This order is also the bit order of kEn*, and the offline renderer used
    // to open-code it. Changing one now changes both.
    if (mask & kEnBass)      m_bass.process(buf);
    if (mask & kEnExciter)   m_exciter.process(buf);
    if (mask & kEnTube)      m_tube.process(buf);
    if (mask & kEnComp)      m_comp.process(buf);
    if (mask & kEnMultiband) m_multi.process(buf);
    if (mask & kEnReverb)    m_reverb.process(buf);
    if (mask & kEnWidth)     m_width.process(buf);
    if (mask & kEnCrossfeed) m_crossfeed.process(buf);
}

} // namespace dreamdsp::dsp
