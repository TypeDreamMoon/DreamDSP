#pragma once

#include <atomic>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

#include "DspTypes.h"
#include "Fft.h"

namespace dreamdsp::dsp {

// Convolution with an arbitrary impulse response.
//
// Uniform-partition overlap-save. The impulse response is cut into blocks of B
// taps, each transformed once when it is loaded; the audio thread transforms
// one input block per B samples and accumulates the products. Cost per sample
// is then O(P) multiply-adds rather than O(T), which is what makes a
// second-long impulse response affordable at all.
//
// Uniform rather than non-uniform partitioning on purpose. Non-uniform schemes
// reach lower latency, but every one of the design's reviewers independently
// named the same most-likely failure: an off-by-one in the tick-derived
// indexing of the different partition sizes. Its symptom is not a crash, it is
// a plausible-sounding, comb-filtered, silently wrong filter -- which is
// precisely the class of failure this feature exists to remove.

struct Convolution {
    enum : uint32_t {
        kCfConvolveLfe = 1u << 0,   // default off; see the channel mapping rules
        kCfForceMono   = 1u << 1,   // use IR channel 0 for every stream channel
        kCfKnown       = 0x00000003u
    };

    // 44 bytes, alignment 4, no padding. Travels inside ParamBlock.
    struct Params {
        float mix = 1.0f;              // +  0
        float trimDb = 0.0f;           // +  4
        uint32_t irGeneration = 0;     // +  8   0 means no IR has ever been set
        uint32_t irRateHz = 0;         // + 12   cross-checked against the blob
        uint32_t irFrames = 0;         // + 16
        uint32_t irChannels = 0;       // + 20
        uint32_t flags = 0;            // + 24   kCf*
        uint8_t irHash[16] = {};       // + 28   the impulse response's identity
    };
};

static_assert(sizeof(Convolution::Params) == 44, "Convolution::Params layout changed");

// Block size from the sample rate alone -- never from the impulse response, the
// channel count or the enable state, so the reported latency is a constant for
// the life of the stream.
inline uint32_t convBlockForRate(double sampleRate) noexcept
{
    uint32_t b = 64;
    while (b < 2048u && double(b) * 187.5 < sampleRate)
        b <<= 1;
    return b;
}

enum : uint32_t {
    kConvKernelMagic = 0x4B564E43u,          // "CNVK"
    kConvMaxChannels = 8u,
    kConvMaxPartitions = 1024u,
    kConvMaxKernelBytes = 8u * 1024u * 1024u
};

// One route per output channel. spec == kConvBypass means the channel is not
// convolved -- it still passes through the alignment delay.
enum : uint8_t { kConvBypass = 0xFFu };

struct ConvRoute {
    uint8_t inCh = 0;                        // which stream channel feeds it
    uint8_t spec = kConvBypass;              // which stored IR spectrum to use
};

// Immutable once built. Allocated as one block, read by the audio thread,
// released by whoever built it. No std::vector, no destructor, nothing the
// audio thread ever writes.
struct ConvKernel {
    uint32_t magic = 0;
    uint32_t arenaBytes = 0;
    uint32_t block = 0;
    uint32_t bins = 0;
    uint32_t padBins = 0;
    uint32_t partitions = 0;
    uint32_t specCount = 0;
    uint32_t taps = 0;                       // at the stream rate, after truncation
    float wetGain = 1.0f;                    // normalisation, folded in once
    float l1 = 0.0f;                         // reported, never enforced
    ConvRoute route[kConvMaxChannels];
    float *re = nullptr;                     // specCount * partitions * padBins
    float *im = nullptr;
};

// Builds a kernel from impulse response taps ALREADY AT THE STREAM RATE.
// Resampling happens before this, because only the caller knows both rates.
//
// Allocates. Never called from the audio thread. Returns nullptr if the
// arguments are unusable or the budget is exceeded.
struct ConvBuildRequest {
    const float *const *irChannels = nullptr;
    int irChannelCount = 0;
    int tapCount = 0;
    int streamChannels = 0;
    uint32_t block = 0;
    uint32_t flags = 0;                      // Convolution::kCf*
    uint32_t channelMask = 0;                // WAVEFORMATEXTENSIBLE dwChannelMask, 0 if unknown
    float wetGain = 1.0f;
    uint32_t maxPartitions = kConvMaxPartitions;
};

ConvKernel *buildConvKernel(const ConvBuildRequest &request);
void destroyConvKernel(ConvKernel *kernel);

// NaN and infinity, by integer inspection of the exponent.
//
// This layer is compiled /fp:fast, under which `v == v` and std::isfinite() may
// be folded away. It matters more here than anywhere else in the chain: every
// other effect washes a bad sample out as its state decays, but one NaN
// entering the frequency-delay line poisons every bin of every partition and
// never leaves until reset() -- a permanent, silent loss of all audio.
inline float scrubNonFinite(float v) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    return ((bits & 0x7F800000u) == 0x7F800000u) ? 0.0f : v;
}

// A hard ceiling on the wet path. clampTo() passes NaN straight through, so the
// scrub has to come first or the ceiling is not a ceiling.
inline float safeWet(float v) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    if ((bits & 0x7F800000u) == 0x7F800000u)
        return 0.0f;
    return v < -4.0f ? -4.0f : (v > 4.0f ? 4.0f : v);   // +12 dBFS
}

class ConvolutionStage
{
public:
    ~ConvolutionStage();

    // Not real-time. Sizes everything for the worst case and clears all state.
    // maxPartitions bounds the frequency-delay line, and so the longest impulse
    // response this stage can ever accept.
    void prepare(double sampleRate, int channels, uint32_t maxPartitions);

    void reset();

    // Real-time safe. Copies the POD and recomputes two scalars; it never
    // installs a kernel and never clears state, so moving the mix slider costs
    // one multiply rather than a rebuild.
    void setParams(const Convolution::Params &p) noexcept;
    void setEnabled(bool on) noexcept;

    // Real-time safe. Runs the whole chain for `buf`.
    void process(const AudioBuffer &buf) noexcept;

    // --- kernel hand-off ---------------------------------------------------
    //
    // The builder pushes a kernel in and collects the displaced one later. The
    // audio thread only ever exchanges two pointers, so neither side blocks.

    // Not real-time. Returns false if a hand-off is already in flight.
    bool offerKernel(ConvKernel *kernel) noexcept;
    // Not real-time. Returns a kernel the audio thread has finished with, or
    // null. The caller owns it and must destroy it.
    ConvKernel *reclaimKernel() noexcept;

    // Armed means an impulse response has been configured at all. An armed
    // stage runs -- and therefore applies its alignment delay -- whether or not
    // convolution is enabled, so the latency reported to Windows is honest for
    // the whole life of the stream rather than changing under it.
    bool armed() const noexcept { return m_armed; }
    void setArmed(bool on) noexcept { m_armed = on; }

    uint32_t block() const noexcept { return m_block; }
    uint32_t latencySamples() const noexcept { return m_armed ? m_block : 0u; }

private:
    void tick(int channels) noexcept;

    float *fdlRe(int ch, uint32_t slot) noexcept
    {
        return m_fdlRe.data() + (size_t(ch) * m_partitions + slot) * m_padBins;
    }
    float *fdlIm(int ch, uint32_t slot) noexcept
    {
        return m_fdlIm.data() + (size_t(ch) * m_partitions + slot) * m_padBins;
    }

    double m_sampleRate = 0.0;
    int m_channels = 0;
    uint32_t m_block = 0;
    uint32_t m_bins = 0;
    uint32_t m_padBins = 0;
    uint32_t m_partitions = 0;      // frequency-delay line depth

    Fft m_fft;

    // All sized in prepare(), never resized.
    std::vector<float> m_inAccum;     // channels * block
    std::vector<float> m_dryDelay;    // channels * block
    std::vector<float> m_outBuf;      // channels * block
    std::vector<float> m_win;         // channels * 2*block
    std::vector<float> m_fdlRe, m_fdlIm;
    std::vector<float> m_accRe, m_accIm;
    std::vector<std::complex<float>> m_cplx;
    std::vector<float> m_tmp;         // 2*block

    uint32_t m_pos = 0;
    uint32_t m_slotIndex = 0;

    // Crossfade between dry and wet, so enabling, disabling or swapping an
    // impulse response never steps the signal.
    float m_fade = 0.0f;
    float m_fadeTarget = 0.0f;
    float m_fadeStep = 0.0f;
    float m_fadeInc = 0.0f;

    bool m_enabled = false;
    bool m_armed = false;
    Convolution::Params m_p;
    float m_trim = 1.0f;

    // Written only by the audio thread.
    ConvKernel *m_current = nullptr;
    ConvKernel *m_pending = nullptr;

    std::atomic<ConvKernel *> m_incoming{ nullptr };
    std::atomic<ConvKernel *> m_retired{ nullptr };
};

} // namespace dreamdsp::dsp
