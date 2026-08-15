#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "Compressor.h"
#include "BassBoost.h"
#include "Convolver.h"
#include "Equalizer.h"
#include "GraphicEq.h"
#include "Limiter.h"
#include "Loudness.h"
#include "MultibandCompressor.h"
#include "Reverb.h"
#include "Routing.h"
#include "Saturation.h"
#include "Stereo.h"

// The complete parameter payload that travels from the GUI to the copy of the
// DSP running inside audiodg.exe.
//
// Parameters travel, not filter coefficients. The file this ends up in sits
// under C:\ProgramData\DreamDSP, where BUILTIN\Users holds Modify -- so anything
// on the wire is attacker-controlled input to a system audio process. A hostile
// IIR denominator cannot be told apart from a legitimate one by any cheap test
// (r = 0.998 is both a 40 Hz highpass at 192 kHz and a +48 dB resonator), which
// would leave an unbounded-gain path into audiodg that no validator could close.
// Twenty-nine clamped scalars can be bounded exactly, and the file stays legible.

namespace dreamdsp::dsp {

enum : uint32_t {
    kParamMagic = 0x42505244u,   // reads as DRPB in a hex dump
    kParamVersion = 5u
};

// One bit per effect. The dsp layer has no enable flag of its own; these bits
// are the entire enable surface.
//
// Bit order is NOT chain order any more: the equalizer was added last and runs
// first. EffectChain::process is the order.
enum : uint32_t {
    kEnBass      = 1u << 0,
    kEnExciter   = 1u << 1,
    kEnTube      = 1u << 2,
    kEnComp      = 1u << 3,
    kEnMultiband = 1u << 4,
    kEnReverb    = 1u << 5,
    kEnWidth     = 1u << 6,
    kEnCrossfeed = 1u << 7,
    // Last in the chain deliberately: the primary use is room, headphone or
    // HRTF correction, which models the transducer and belongs immediately
    // before the DAC.
    kEnConvolution = 1u << 8,
    // Runs first, ahead of every bit above it. Until this existed the equalizer
    // was `Filter N:` text handed to Equalizer APO, which is what made APO a
    // runtime dependency rather than an import format.
    kEnEqualizer = 1u << 9,
    // Wiring rather than tone, and last in the chain for that reason: which
    // speaker a stream channel ends up in, and how far away it is.
    kEnMatrix    = 1u << 10,
    kEnDelay     = 1u << 11,
    // Sits with the equalizer, because it is an equalizer -- one whose curve is
    // a function of how far the volume control has been turned down.
    kEnLoudness  = 1u << 12,
    kEnLimiter   = 1u << 13,
    kEnDynBass   = 1u << 14,
    kEnGraphic   = 1u << 15,
    kEnKnown     = 0x0000FFFFu
};

// One id per stage, and the entire vocabulary of the order array below. These
// are on the wire, so the values are fixed; the order they run in is not.
enum : uint8_t {
    kStageEqualizer = 0,
    kStageGraphic,
    kStageLoudness,
    kStageDynBass,
    kStageBass,
    kStageExciter,
    kStageTube,
    kStageComp,
    kStageMultiband,
    kStageReverb,
    kStageWidth,
    kStageCrossfeed,
    kStageMatrix,
    kStageConvolution,
    kStageDelay,
    kStageLimiter,
    kStageCount
};
static_assert(kStageCount == 16, "kDefaultOrder and ParamBlock::order are sized for 16");

// The order the stages run in when nobody has said otherwise. Reproduces what
// the chain did when it was hard-coded, and is what the sanitiser falls back to
// for any order array that is not a permutation.
extern const uint8_t kDefaultOrder[kStageCount];

// Byte-for-byte what is on disk. There is no serialisation step: every member is
// trivially copyable, alignment 4, and every member size is a multiple of 4, so
// there is no padding between members either.
//
// The Params structs are embedded by value on purpose. Adding a field to any of
// them changes sizeof(ParamBlock) and breaks the static_assert below in both the
// GUI and the APO at once -- which is the only thing in this design that keeps
// the two ends in step and cannot be forgotten.
struct ParamBlock {
    uint32_t magic;        // +  0
    uint32_t version;      // +  4   exact match required, not >=
    uint32_t sizeBytes;    // +  8   catches truncation
    uint32_t generation;   // + 12   strictly increasing
    uint32_t enableMask;   // + 16   kEn* bits; 0 means fully transparent
    uint32_t reserved0;    // + 20

    Compressor::Params          comp;        // + 24    28
    Reverb::Params              reverb;      // + 52    32
    TubeStage::Params           tube;        // + 84    16
    Exciter::Params             exciter;     // +100    12
    VirtualBass::Params         bass;        // +112    16
    StereoWidener::Params       width;       // +128     8
    Crossfeed::Params           crossfeed;   // +136    12
    MultibandCompressor::Params multiband;   // +148   108
    Convolution::Params         convolution; // +256    44
    // Appended rather than placed in chain order. Struct order carries no
    // meaning -- EffectChain::process is the order -- and appending leaves every
    // offset above unchanged, so the assertions below still guard what they did.
    Equalizer::Params           eq;          // +300   520
    ChannelMatrix::Params       matrix;      // +820   256
    ChannelDelay::Params        delay;       //+1076    32
    LoudnessCorrection::Params  loudness;    //+1108    16
    Limiter::Params             limiter;     //+1124    16
    DynamicBass::Params         dynBass;     //+1140    16
    GraphicEq::Params           graphic;     //+1156   128

    // Which stage runs when. A permutation of 0..kStageCount-1; anything else
    // is replaced wholesale by kDefaultOrder, because a duplicate would run a
    // stage twice and an omission would silently drop an effect whose switch
    // says it is on.
    uint8_t order[kStageCount];              //+1284    16
};                                           // = 1300

static_assert(sizeof(ParamBlock) == 1300, "wire layout changed; bump kParamVersion");
static_assert(alignof(ParamBlock) == 4, "wire layout changed; bump kParamVersion");
static_assert(std::is_trivially_copyable<ParamBlock>::value, "must be memcpy-able");
static_assert(offsetof(ParamBlock, enableMask) == 16, "wire layout changed");
static_assert(offsetof(ParamBlock, comp) == 24, "wire layout changed");
static_assert(offsetof(ParamBlock, reverb) == 52, "wire layout changed");
static_assert(offsetof(ParamBlock, tube) == 84, "wire layout changed");
static_assert(offsetof(ParamBlock, exciter) == 100, "wire layout changed");
static_assert(offsetof(ParamBlock, bass) == 112, "wire layout changed");
static_assert(offsetof(ParamBlock, width) == 128, "wire layout changed");
static_assert(offsetof(ParamBlock, crossfeed) == 136, "wire layout changed");
static_assert(offsetof(ParamBlock, multiband) == 148, "wire layout changed");
static_assert(offsetof(ParamBlock, convolution) == 256, "wire layout changed");
static_assert(offsetof(ParamBlock, eq) == 300, "wire layout changed");
static_assert(offsetof(ParamBlock, matrix) == 820, "wire layout changed");
static_assert(offsetof(ParamBlock, delay) == 1076, "wire layout changed");
static_assert(offsetof(ParamBlock, loudness) == 1108, "wire layout changed");
static_assert(offsetof(ParamBlock, limiter) == 1124, "wire layout changed");
static_assert(offsetof(ParamBlock, dynBass) == 1140, "wire layout changed");
static_assert(offsetof(ParamBlock, graphic) == 1156, "wire layout changed");
static_assert(offsetof(ParamBlock, order) == 1284, "wire layout changed");

// --------------------------------------------------------------- sanitising

// Clamp to [lo, hi], substituting `dflt` for anything that is not a finite
// number.
//
// NaN and infinity are detected by inspecting the exponent field, not by
// comparison. This layer is compiled with /fp:fast, under which `v == v`,
// std::isfinite(v) and `!(v > -FLT_MAX && v < FLT_MAX)` may all be folded away
// to a constant. Integer inspection cannot be.
//
// clampTo() from DspTypes.h must not be used for this: its form
// `v < lo ? lo : (v > hi ? hi : v)` passes NaN straight through, and one NaN in
// the reverb's comb feedback poisons all 26 delay lines until the next reset.
inline float sanF(float v, float lo, float hi, float dflt) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    if ((bits & 0x7F800000u) == 0x7F800000u)   // NaN or infinity
        return dflt;
    return v < lo ? lo : (v > hi ? hi : v);
}

// A bool read off the wire is a byte, and a byte can hold 2. Anything non-zero
// becomes true, so the in-memory representation is always exactly 0 or 1.
inline bool sanB(bool v) noexcept
{
    unsigned char byte;
    std::memcpy(&byte, &v, 1);
    return byte != 0;
}

// Returns a block in which every field is finite and in range, the header
// fields hold the constants, and enableMask contains only known bits.
//
// Idempotent: sanitise(sanitise(x)) is byte-identical to sanitise(x). That is
// what makes EffectChain's memcmp change-detection stable.
ParamBlock sanitise(const ParamBlock &in) noexcept;

// True when `b` is exactly what sanitise() would have produced. Used by the
// tests and by the assertions; the runtime path sanitises unconditionally
// rather than checking first.
bool isSane(const ParamBlock &b) noexcept;

// The block that means "pass audio through untouched": all zero except the
// header. Deliberately NOT `ParamBlock{}` -- the members carry default member
// initialisers (reverb.wet = 0.3f, ratio = 1.0f, bandEnabled = {true,true,true}),
// so value-initialisation would produce an always-on reverb.
ParamBlock transparentBlock() noexcept;

// Applies the bounds that depend on the sample rate, which the sanitiser cannot
// know. Called by EffectChain::apply, after prepare() has established the rate.
void clampForRate(ParamBlock &b, double sampleRate) noexcept;

} // namespace dreamdsp::dsp
