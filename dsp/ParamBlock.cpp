#include "ParamBlock.h"

namespace dreamdsp::dsp {

// Correction first, then things that add content, then dynamics, then the
// output stage. Reproduces the order the chain ran in when it was hard-coded.
const uint8_t kDefaultOrder[kStageCount] = {
    // Levelling first, so every stage downstream sees a signal at a predictable
    // level. This matters most for the ones whose behaviour is a function of
    // absolute level -- the compressor and the night-mode curve -- and it is
    // the order Orban's broadcast chain uses for exactly that reason.
    kStageAutoGain,
    kStageEqualizer, kStageGraphic, kStageLoudness,
    kStageDynBass, kStageBass, kStageExciter, kStageTube,
    // Envelope shaping ahead of the compressor, so the compressor sees the
    // shape we asked for rather than fighting it.
    kStageTransient,
    kStageComp, kStageMultiband,
    // Night mode after the compressors: it is a *decoder* function, and its
    // curve is calibrated against programme loudness, not against whatever the
    // multiband happened to leave behind.
    kStageNightMode,
    kStageReverb,
    kStageWidth, kStageCrossfeed,
    kStageMatrix, kStageConvolution, kStageDelay,
    // Clipper immediately before the limiter. At one decibel of peak reduction
    // the clipper adds 0.1 dB of modulation to sustained content where the
    // limiter adds 17 to 20; putting it first means the limiter only ever has
    // slow, shallow, well-masked work left to do.
    kStageClipper, kStageLimiter
};

namespace {

// True when `order` names every stage exactly once.
//
// Checked rather than repaired, and replaced wholesale when it fails. A partial
// repair would produce an order the user never asked for and could not predict,
// where the documented fallback is at least the one the interface shows as the
// default.
bool isPermutation(const uint8_t *order) noexcept
{
    uint32_t seen = 0;
    for (int i = 0; i < kStageCount; ++i) {
        if (order[i] >= kStageCount)
            return false;
        const uint32_t bit = 1u << order[i];
        if (seen & bit)
            return false;
        seen |= bit;
    }
    return true;
}

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
    o.flags = in.flags & kPfKnown;

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
    // -120 dB is the "off" end: below any signal the stage will ever see, so
    // the shaper is always engaged and the stage behaves as it did before the
    // threshold existed.
    o.exciter.thresholdDb = sanF(in.exciter.thresholdDb, -120.0f, -12.0f, -45.0f);

    o.bass.cutoffHz      = sanF(in.bass.cutoffHz, 20.0f, 400.0f, 90.0f);
    o.bass.amount        = sanF(in.bass.amount, 0.0f, 1.0f, 0.0f);
    o.bass.drive         = sanF(in.bass.drive, 0.1f, 32.0f, 1.0f);
    o.bass.removeOriginal = sanB(in.bass.removeOriginal);

    o.width.width = sanF(in.width.width, 0.0f, 2.0f, 1.0f);
    o.width.monoBelowHz = sanF(in.width.monoBelowHz, 0.0f, 500.0f, 0.0f);
    o.width.phaseAmount = sanF(in.width.phaseAmount, 0.0f, 1.0f, 0.0f);
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

    // Preamp is a straight multiply, but the equalizer rails its own output at
    // +12 dBFS, so this bound only has to be sane rather than to be the only
    // thing standing between a bad file and a full-scale blast.
    o.eq.preampDb = sanF(in.eq.preampDb, -60.0f, 30.0f, 0.0f);
    o.eq.bandCount = in.eq.bandCount > uint32_t(Equalizer::kMaxBands)
                         ? uint32_t(Equalizer::kMaxBands) : in.eq.bandCount;
    for (uint32_t i = 0; i < o.eq.bandCount; ++i) {
        const EqBand &s = in.eq.band[i];
        EqBand &d = o.eq.band[i];
        // Never zero, so that a band inside bandCount can never be
        // byte-identical to an unused slot -- which is what lets the equalizer
        // use memcmp against its own copy to decide what to redesign.
        d.freqHz = sanF(s.freqHz, 10.0f, 400000.0f, 1000.0f);
        d.gainDb = sanF(s.gainDb, -40.0f, 40.0f, 0.0f);
        d.q      = sanF(s.q, 0.05f, 100.0f, 1.0f);
        d.type   = s.type < uint8_t(FilterKind::Count) ? s.type : 0u;
        d.enabled = sanB(s.enabled) ? 1u : 0u;
        d.reserved = 0;
    }
    // Bands past the count stay as memset left them. An unused slot has to hold
    // a fixed byte pattern, or the block's on-disk representation would depend
    // on whatever the GUI happened to have in those slots earlier.

    // Eight inputs at +12 dB each would be +30 dB of summing gain, so each
    // coefficient is bounded and the stage rails its own output as well. Note
    // that the neutral matrix is the identity, not zero -- an all-zero block
    // therefore describes silence, which is why the stage means nothing without
    // kEnMatrix and why silence is the direction it fails in.
    for (int o2 = 0; o2 < ChannelMatrix::kMaxChannels; ++o2)
        for (int i = 0; i < ChannelMatrix::kMaxChannels; ++i)
            o.matrix.gain[o2][i] = sanF(in.matrix.gain[o2][i], -4.0f, 4.0f,
                                        o2 == i ? 1.0f : 0.0f);

    for (int c = 0; c < ChannelDelay::kMaxChannels; ++c)
        o.delay.ms[c] = sanF(in.delay.ms[c], 0.0f, ChannelDelay::kMaxMs, 0.0f);

    // volumeDb is an attenuation reported by the endpoint, so it is at most 0.
    // The floor is generous: some devices report a range down to -96 dB.
    o.loudness.volumeDb   = sanF(in.loudness.volumeDb, -120.0f, 0.0f, 0.0f);
    o.loudness.referenceDb = sanF(in.loudness.referenceDb, -120.0f, 0.0f, 0.0f);
    o.loudness.offsetDb   = sanF(in.loudness.offsetDb, -60.0f, 60.0f, 0.0f);
    o.loudness.amount     = sanF(in.loudness.amount, 0.0f, 1.0f, 0.0f);

    // The limiter's threshold is a promise the stage keeps exactly, so the only
    // job here is to keep it inside a range where that promise is meaningful.
    o.limiter.gainDb      = sanF(in.limiter.gainDb, -24.0f, 24.0f, 0.0f);
    o.limiter.thresholdDb = sanF(in.limiter.thresholdDb, -30.0f, 0.0f, -0.3f);
    o.limiter.releaseMs   = sanF(in.limiter.releaseMs, 1.0f, 1000.0f, 100.0f);
    o.limiter.lookaheadMs = sanF(in.limiter.lookaheadMs, 0.0f, Limiter::kMaxLookaheadMs, 1.5f);
    o.limiter.truePeak    = sanB(in.limiter.truePeak);
    o.limiter.pad[0] = o.limiter.pad[1] = o.limiter.pad[2] = 0;

    o.dynBass.maxGainDb = sanF(in.dynBass.maxGainDb, 0.0f, 24.0f, 0.0f);
    o.dynBass.cutoffHz  = sanF(in.dynBass.cutoffHz, 30.0f, 250.0f, 100.0f);
    o.dynBass.releaseMs = sanF(in.dynBass.releaseMs, 10.0f, 2000.0f, 250.0f);
    o.dynBass.reserved  = 0.0f;

    // Thirty-one bounded decibel values. This is the whole reason the graphic
    // equalizer is a fixed band grid rather than an impulse response: a curve
    // can be clamped exactly, and a set of filter coefficients cannot.
    for (int i = 0; i < GraphicEq::kBands; ++i)
        o.graphic.gainDb[i] = sanF(in.graphic.gainDb[i], -40.0f, 40.0f, 0.0f);
    o.graphic.amount = sanF(in.graphic.amount, 0.0f, 1.0f, 0.0f);

    // 1 ms and 5 s are Calf's published bounds for the same two controls, and
    // the knobs are the +/-1 the wire format wants everywhere else.
    o.transient.attack      = sanF(in.transient.attack, -1.0f, 1.0f, 0.0f);
    o.transient.sustain     = sanF(in.transient.sustain, -1.0f, 1.0f, 0.0f);
    o.transient.attackMs    = sanF(in.transient.attackMs, 1.0f, 500.0f, 30.0f);
    o.transient.releaseMs   = sanF(in.transient.releaseMs, 1.0f, 5000.0f, 300.0f);
    o.transient.thresholdDb = sanF(in.transient.thresholdDb, -90.0f, -20.0f, -60.0f);

    // The knee is bounded at 6 dB rather than left open: past about 2 dB this
    // stops behaving like a clipper and starts behaving like a saturator, and
    // a saturator in front of the limiter is the configuration the whole stage
    // exists to avoid.
    o.clipper.driveDb   = sanF(in.clipper.driveDb, 0.0f, 24.0f, 0.0f);
    o.clipper.ceilingDb = sanF(in.clipper.ceilingDb, -24.0f, 0.0f, -0.3f);
    o.clipper.kneeDb    = sanF(in.clipper.kneeDb, 0.0f, 6.0f, 1.0f);
    o.clipper.oversample = sanB(in.clipper.oversample);
    o.clipper.pad[0] = o.clipper.pad[1] = o.clipper.pad[2] = 0;

    // 20 dB/s is the aggressive end of Orban's AGC range and is audibly a
    // compressor; 0.25 is slower than anything that ships. Both are bounds, not
    // recommendations -- the default of 3 dB/s sits where four independent
    // implementations converged.
    o.autoGain.targetLufs   = sanF(in.autoGain.targetLufs, -40.0f, -10.0f, -20.0f);
    o.autoGain.maxGainDb    = sanF(in.autoGain.maxGainDb, 0.0f, 25.0f, 12.0f);
    o.autoGain.rateDbPerSec = sanF(in.autoGain.rateDbPerSec, 0.25f, 20.0f, 3.0f);
    o.autoGain.windowDb     = sanF(in.autoGain.windowDb, 0.0f, 12.0f, 3.0f);
    o.autoGain.gateLufs     = sanF(in.autoGain.gateLufs, -70.0f, -20.0f, -50.0f);

    // The profile is an index into a fixed table of five published curves, so
    // an out-of-range value becomes "none" rather than being clamped into a
    // neighbouring profile the user did not choose.
    o.nightMode.profile = (in.nightMode.profile > DynamicRange::kProfileNone
                           && in.nightMode.profile < DynamicRange::kProfileCount)
                              ? in.nightMode.profile
                              : int32_t(DynamicRange::kProfileNone);
    o.nightMode.boost         = sanF(in.nightMode.boost, 0.0f, 1.0f, 1.0f);
    o.nightMode.cut           = sanF(in.nightMode.cut, 0.0f, 1.0f, 1.0f);
    o.nightMode.referenceLufs = sanF(in.nightMode.referenceLufs, -40.0f, -10.0f, -24.0f);

    if (isPermutation(in.order))
        std::memcpy(o.order, in.order, sizeof o.order);
    else
        std::memcpy(o.order, kDefaultOrder, sizeof o.order);

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

    // The channel matrix is the one stage whose neutral setting is not all
    // zero: zero means silence, identity means "leave the channels alone". It
    // is set here rather than in sanitise() so that muting every channel stays
    // expressible -- a block that says silence is allowed to mean silence.
    //
    // With this, the block is transparent for every enableMask rather than only
    // for zero, which is what the transparency test asserts.
    zero.matrix = ChannelMatrix::identity();

    // An all-zero order array names stage 0 sixteen times, which the sanitiser
    // would replace anyway -- setting it here makes the block a fixed point of
    // sanitise() rather than something that changes on its first pass through.
    std::memcpy(zero.order, kDefaultOrder, sizeof zero.order);

    // Passed through the sanitiser rather than returned raw, so that the block
    // satisfies isSane(). That matters beyond tidiness: the argument that a
    // torn read can never produce a dangerous parameter set rests on every slot
    // always holding a *sanitised* block, and an all-zero block is not one --
    // its compressor ratio of 0 is outside the legal range.
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

    // The equalizer's band frequencies are deliberately left alone. Every other
    // stage here designs its filters without checking Nyquist, so this is the
    // only place that can bound them; designFilter() does check, and clamping to
    // 0.45 * rate on top of it would move a 20 kHz high shelf on a 44.1 kHz
    // endpoint for no reason -- and move it by a different amount per endpoint,
    // so the same preset would not be the same filter on two devices.
}

} // namespace dreamdsp::dsp
