#include "ParamBlock.h"

namespace dreamdsp::dsp {

namespace {

// The rule, applied mechanically below:
//
// Where the dsp layer clamps a value internally, the bound here is deliberately
// wider than both the GUI slider and the internal clamp. It exists only to keep
// the value finite and sane, so that widening a slider later cannot be silently
// undone here.
//
// Where the dsp layer does NOT clamp and the value reaches a gain multiply, the
// bound here is the only bound there is, and is deliberately tight. Those are
// comp.makeupDb, tube.outputDb, reverb.dry and multiband.bandGainDb.
void sanitiseCompressor(Compressor::Params &o, const Compressor::Params &i,
                        float defThresholdDb, float defKneeDb,
                        float defAttackMs, float defReleaseMs) noexcept
{
    o.thresholdDb = sanF(i.thresholdDb, -96.0f, 24.0f, defThresholdDb);
    o.ratio       = sanF(i.ratio, 1.0f, 100.0f, 1.0f);
    o.kneeDb      = sanF(i.kneeDb, 0.0f, 24.0f, defKneeDb);
    o.attackMs    = sanF(i.attackMs, 0.1f, 200.0f, defAttackMs);
    o.releaseMs   = sanF(i.releaseMs, 1.0f, 2000.0f, defReleaseMs);
    // Unclamped in dsp/ and feeds dbToLin directly: this is the gain bound.
    o.makeupDb    = sanF(i.makeupDb, -24.0f, 24.0f, 0.0f);

    o.autoKnee    = sanB(i.autoKnee);
    o.autoAttack  = sanB(i.autoAttack);
    o.autoRelease = sanB(i.autoRelease);
    o.autoMakeup  = sanB(i.autoMakeup);
}

} // namespace

ParamBlock sanitise(const ParamBlock &in) noexcept
{
    // An explicit memset, not `ParamBlock o{}`: the members carry default member
    // initialisers, so value-initialisation would seed an always-on reverb. This
    // also guarantees any padding is zero, which keeps the on-disk bytes stable.
    ParamBlock o;
    std::memset(&o, 0, sizeof o);

    // Header fields come from the constants, never from the input.
    o.magic = kParamMagic;
    o.version = kParamVersion;
    o.sizeBytes = uint32_t(sizeof(ParamBlock));
    o.generation = in.generation;
    o.enableMask = in.enableMask & kEnKnown;
    o.reserved0 = 0;

    sanitiseCompressor(o.comp, in.comp, 0.0f, 0.0f, 5.0f, 50.0f);

    o.reverb.roomSize   = sanF(in.reverb.roomSize, 0.0f, 1.0f, 0.5f);
    o.reverb.damping    = sanF(in.reverb.damping, 0.0f, 1.0f, 0.5f);
    o.reverb.density    = sanF(in.reverb.density, 0.0f, 1.0f, 0.5f);
    o.reverb.bandwidth  = sanF(in.reverb.bandwidth, 0.0f, 1.0f, 1.0f);
    o.reverb.preDelayMs = sanF(in.reverb.preDelayMs, 0.0f, 200.0f, 0.0f);
    o.reverb.width      = sanF(in.reverb.width, 0.0f, 1.0f, 1.0f);
    o.reverb.wet        = sanF(in.reverb.wet, 0.0f, 1.0f, 0.0f);
    // Used raw as a multiplier on the dry path; nothing downstream clamps it.
    o.reverb.dry        = sanF(in.reverb.dry, 0.0f, 1.0f, 1.0f);

    o.tube.drive    = sanF(in.tube.drive, 0.1f, 32.0f, 2.0f);
    o.tube.bias     = sanF(in.tube.bias, 0.0f, 2.0f, 0.0f);
    o.tube.mix      = sanF(in.tube.mix, 0.0f, 1.0f, 0.0f);
    o.tube.outputDb = sanF(in.tube.outputDb, -24.0f, 12.0f, 0.0f);   // gain bound

    o.exciter.frequencyHz = sanF(in.exciter.frequencyHz, 20.0f, 20000.0f, 3500.0f);
    o.exciter.drive       = sanF(in.exciter.drive, 1.0f, 32.0f, 1.0f);
    o.exciter.amount      = sanF(in.exciter.amount, 0.0f, 1.0f, 0.0f);

    o.bass.cutoffHz      = sanF(in.bass.cutoffHz, 20.0f, 400.0f, 90.0f);
    o.bass.amount        = sanF(in.bass.amount, 0.0f, 1.0f, 0.0f);
    o.bass.drive         = sanF(in.bass.drive, 0.1f, 32.0f, 1.0f);
    o.bass.removeOriginal = sanB(in.bass.removeOriginal);

    o.width.width = sanF(in.width.width, 0.0f, 2.0f, 1.0f);
    o.width.monoBelowHz = sanF(in.width.monoBelowHz, 0.0f, 500.0f, 0.0f);
    // The widener switches the filter on for any value > 0 but designs it on
    // clamp(20, 500) -- so 0.001 Hz would silently engage a 20 Hz filter. Snap
    // the dead zone to off instead.
    if (o.width.monoBelowHz < 20.0f)
        o.width.monoBelowHz = 0.0f;

    o.crossfeed.cutoffHz = sanF(in.crossfeed.cutoffHz, 20.0f, 4000.0f, 700.0f);
    o.crossfeed.feedDb   = sanF(in.crossfeed.feedDb, -60.0f, 0.0f, -4.5f);
    o.crossfeed.delayUs  = sanF(in.crossfeed.delayUs, 0.0f, 2000.0f, 300.0f);

    o.multiband.lowCrossHz = sanF(in.multiband.lowCrossHz, 20.0f, 2000.0f, 250.0f);
    // Clamped after the low crossover, because its floor derives from it: the
    // band splitter needs the two to stay apart.
    {
        const float floorHz = o.multiband.lowCrossHz * 1.5f;
        const float lo = floorHz > 200.0f ? floorHz : 200.0f;
        o.multiband.highCrossHz = sanF(in.multiband.highCrossHz, lo, 20000.0f, 3000.0f);
        if (o.multiband.highCrossHz < lo)
            o.multiband.highCrossHz = lo;
    }
    for (int b = 0; b < MultibandCompressor::kBands; ++b) {
        sanitiseCompressor(o.multiband.band[b], in.multiband.band[b],
                           -18.0f, 6.0f, 10.0f, 120.0f);
        o.multiband.bandGainDb[b] = sanF(in.multiband.bandGainDb[b], -24.0f, 12.0f, 0.0f);
        o.multiband.bandEnabled[b] = sanB(in.multiband.bandEnabled[b]);
    }

    o.convolution.mix = sanF(in.convolution.mix, 0.0f, 1.0f, 1.0f);
    o.convolution.trimDb = sanF(in.convolution.trimDb, -24.0f, 12.0f, 0.0f);
    o.convolution.irGeneration = in.convolution.irGeneration;
    o.convolution.irFrames = in.convolution.irFrames > kIrMaxFrames
                                 ? kIrMaxFrames : in.convolution.irFrames;
    o.convolution.irChannels = in.convolution.irChannels > 8u ? 8u : in.convolution.irChannels;
    // Zero means "unknown"; anything outside the plausible range is treated the
    // same way rather than being clamped to a rate the file does not have.
    o.convolution.irRateHz = (in.convolution.irRateHz >= 8000u
                              && in.convolution.irRateHz <= 384000u)
                                 ? in.convolution.irRateHz : 0u;
    o.convolution.flags = in.convolution.flags & Convolution::kCfKnown;
    // The hash is opaque bytes. Sanitising it would be meaningless -- it is
    // verified against the blob's contents instead.
    std::memcpy(o.convolution.irHash, in.convolution.irHash, sizeof o.convolution.irHash);

    return o;
}

bool isSane(const ParamBlock &b) noexcept
{
    const ParamBlock s = sanitise(b);
    return std::memcmp(&s, &b, sizeof(ParamBlock)) == 0;
}

ParamBlock transparentBlock() noexcept
{
    ParamBlock zero;
    std::memset(&zero, 0, sizeof zero);

    // Passed through the sanitiser rather than returned raw, so that the block
    // satisfies isSane(). That matters beyond tidiness: the argument that a
    // torn read can never produce a dangerous parameter set rests on every slot
    // always holding a *sanitised* block, and an all-zero block is not one --
    // its compressor ratio of 0 is outside the legal range.
    //
    // Transparency does not come from the field values, it comes from
    // enableMask being 0, which survives sanitising untouched.
    return sanitise(zero);
}

void clampForRate(ParamBlock &b, double sampleRate) noexcept
{
    if (!(sampleRate > 0.0))
        return;

    // Keep every designed filter below Nyquist with margin. A biquad designed at
    // or above Nyquist is not merely wrong, it can be unstable.
    const float maxHz = float(sampleRate) * 0.45f;

    if (b.exciter.frequencyHz > maxHz)
        b.exciter.frequencyHz = maxHz;
    if (b.bass.cutoffHz > maxHz)
        b.bass.cutoffHz = maxHz;
    if (b.crossfeed.cutoffHz > maxHz)
        b.crossfeed.cutoffHz = maxHz;
    if (b.width.monoBelowHz > maxHz)
        b.width.monoBelowHz = maxHz;

    if (b.multiband.highCrossHz > maxHz)
        b.multiband.highCrossHz = maxHz;
    // The low crossover has to stay below the high one even after that clamp,
    // or the band splitter produces a band with negative width.
    const float lowMax = b.multiband.highCrossHz / 1.5f;
    if (b.multiband.lowCrossHz > lowMax)
        b.multiband.lowCrossHz = lowMax;
}

} // namespace dreamdsp::dsp
