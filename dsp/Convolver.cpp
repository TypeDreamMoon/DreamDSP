#include "Convolver.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace dreamdsp::dsp {

namespace {

// Rounded up so every partition row starts on a 64-byte boundary and the
// multiply-accumulate loop runs to a multiple of 16 with zeros in the padding.
uint32_t padBinsFor(uint32_t bins) { return (bins + 15u) & ~15u; }

size_t alignUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

// SPEAKER_LOW_FREQUENCY in the WAVEFORMATEXTENSIBLE channel mask.
constexpr uint32_t kSpeakerLowFrequency = 0x8u;

// Which stream channel index the LFE occupies, or -1. Channels appear in the
// order of the set bits in the mask.
int lfeIndexFromMask(uint32_t mask, int channels)
{
    if (mask == 0)
        return -1;
    int index = 0;
    for (uint32_t bit = 1; bit; bit <<= 1) {
        if (!(mask & bit))
            continue;
        if (bit == kSpeakerLowFrequency)
            return (index < channels) ? index : -1;
        ++index;
    }
    return -1;
}

} // namespace

// --------------------------------------------------------------- kernel build

ConvKernel *buildConvKernel(const ConvBuildRequest &r)
{
    if (!r.irChannels || r.irChannelCount < 1 || r.tapCount < 1
        || r.streamChannels < 1 || r.streamChannels > int(kConvMaxChannels)
        || r.block < 1 || (r.block & (r.block - 1)) != 0) {
        return nullptr;
    }

    const uint32_t block = r.block;
    const uint32_t bins = block + 1u;
    const uint32_t padBins = padBinsFor(bins);

    const uint32_t specCount = uint32_t(std::min(r.irChannelCount, int(kConvMaxChannels)));
    const uint32_t partitions =
        uint32_t((uint64_t(r.tapCount) + block - 1u) / block);
    if (partitions < 1 || partitions > r.maxPartitions || partitions > kConvMaxPartitions)
        return nullptr;

    const size_t headerBytes = alignUp(sizeof(ConvKernel), 64);
    const size_t planeBytes = size_t(specCount) * partitions * padBins * sizeof(float);
    const size_t total = headerBytes + 2 * planeBytes + 64;
    if (total > kConvMaxKernelBytes)
        return nullptr;

    auto *arena = new (std::nothrow) unsigned char[total];
    if (!arena)
        return nullptr;
    std::memset(arena, 0, total);

    auto *kernel = reinterpret_cast<ConvKernel *>(arena);
    // Placement-new so the members with default initialisers are live; the
    // memset above already guarantees the payload is zero.
    new (kernel) ConvKernel();

    kernel->magic = kConvKernelMagic;
    kernel->arenaBytes = uint32_t(total);
    kernel->block = block;
    kernel->bins = bins;
    kernel->padBins = padBins;
    kernel->partitions = partitions;
    kernel->specCount = specCount;
    kernel->taps = uint32_t(r.tapCount);
    kernel->wetGain = r.wetGain;

    unsigned char *plane = arena + headerBytes;
    plane += (64 - (reinterpret_cast<uintptr_t>(plane) & 63)) & 63;
    kernel->re = reinterpret_cast<float *>(plane);
    kernel->im = kernel->re + size_t(specCount) * partitions * padBins;

    // --- routing -----------------------------------------------------------
    //
    // Exactly one route per output channel. Never round-robin over a mismatched
    // channel count the way Equalizer APO does -- that is silently wrong for
    // anyone pointing at a four-channel file -- and never synthesise a channel
    // by averaging two decorrelated impulse responses, which is a comb filter.
    const int lfe = lfeIndexFromMask(r.channelMask, r.streamChannels);
    const bool forceMono = (r.flags & Convolution::kCfForceMono) != 0;
    const bool convolveLfe = (r.flags & Convolution::kCfConvolveLfe) != 0;

    for (int c = 0; c < r.streamChannels; ++c) {
        ConvRoute route;
        route.inCh = uint8_t(c);

        if (c == lfe && !convolveLfe) {
            // A full-range room response contributes nothing below 120 Hz the
            // mains do not already carry, and its group delay would decorrelate
            // the LFE from them.
            route.spec = kConvBypass;
        } else if (forceMono || specCount == 1) {
            route.spec = 0;
        } else if (uint32_t(c) < specCount) {
            route.spec = uint8_t(c);
        } else {
            // Fewer IR channels than stream channels: split left and right
            // rather than inventing anything.
            route.spec = uint8_t((c % 2 == 0) ? 0 : std::min(1u, specCount - 1));
        }
        kernel->route[c] = route;
    }

    // --- partition and transform -------------------------------------------
    // Not `Fft fft(int(block))`: that parses as a function declaration.
    Fft fft;
    fft.prepare(int(block));
    std::vector<float> pad(size_t(2 * block), 0.0f);
    std::vector<std::complex<float>> spectrum(size_t(bins), std::complex<float>(0.0f, 0.0f));

    double l1 = 0.0;
    for (uint32_t s = 0; s < specCount; ++s) {
        const float *taps = r.irChannels[s];
        if (!taps)
            continue;
        for (uint32_t p = 0; p < partitions; ++p) {
            std::fill(pad.begin(), pad.end(), 0.0f);
            const uint32_t offset = p * block;
            const uint32_t count = std::min(block, uint32_t(r.tapCount) - offset);
            // Taps in the first half, zeros in the second: that is what makes
            // the second half of the inverse transform the linear convolution
            // and the first half the circular wraparound to be discarded.
            for (uint32_t i = 0; i < count; ++i) {
                const float v = scrubNonFinite(taps[offset + i]);
                pad[i] = v;
                l1 += std::fabs(double(v));
            }
            fft.realForward(pad.data(), spectrum.data());

            float *re = kernel->re + (size_t(s) * partitions + p) * padBins;
            float *im = kernel->im + (size_t(s) * partitions + p) * padBins;
            for (uint32_t b = 0; b < bins; ++b) {
                re[b] = spectrum[b].real();
                im[b] = spectrum[b].imag();
            }
            // [bins, padBins) stay zero, so the padded tail of the MAC loop
            // contributes nothing.
        }
    }
    kernel->l1 = float(l1 / double(specCount));

    return kernel;
}

void destroyConvKernel(ConvKernel *kernel)
{
    if (!kernel)
        return;
    kernel->~ConvKernel();
    delete[] reinterpret_cast<unsigned char *>(kernel);
}

// ---------------------------------------------------------------- the stage

ConvolutionStage::~ConvolutionStage()
{
    destroyConvKernel(m_current);
    destroyConvKernel(m_pending);
    destroyConvKernel(m_incoming.exchange(nullptr));
    destroyConvKernel(m_retired.exchange(nullptr));
}

void ConvolutionStage::prepare(double sampleRate, int channels, uint32_t maxPartitions)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_channels = clampTo(channels, 1, int(kConvMaxChannels));
    m_block = convBlockForRate(m_sampleRate);
    m_bins = m_block + 1u;
    m_padBins = padBinsFor(m_bins);
    m_partitions = maxPartitions < 1u ? 1u
                                 : std::min(maxPartitions, uint32_t(kConvMaxPartitions));

    m_fft.prepare(int(m_block));

    const size_t ch = size_t(m_channels);
    m_inAccum.assign(ch * m_block, 0.0f);
    m_dryDelay.assign(ch * m_block, 0.0f);
    m_outBuf.assign(ch * m_block, 0.0f);
    m_win.assign(ch * 2 * m_block, 0.0f);
    m_fdlRe.assign(ch * m_partitions * m_padBins, 0.0f);
    m_fdlIm.assign(ch * m_partitions * m_padBins, 0.0f);
    m_accRe.assign(m_padBins, 0.0f);
    m_accIm.assign(m_padBins, 0.0f);
    m_cplx.assign(m_bins, std::complex<float>(0.0f, 0.0f));
    m_tmp.assign(size_t(2 * m_block), 0.0f);

    // A 16 ms ramp: above the threshold where a gain step clicks, below the
    // point where the transition itself becomes noticeable.
    m_fadeInc = float(1.0 / (0.016 * m_sampleRate));

    reset();
}

void ConvolutionStage::reset()
{
    std::fill(m_inAccum.begin(), m_inAccum.end(), 0.0f);
    std::fill(m_dryDelay.begin(), m_dryDelay.end(), 0.0f);
    std::fill(m_outBuf.begin(), m_outBuf.end(), 0.0f);
    std::fill(m_win.begin(), m_win.end(), 0.0f);
    std::fill(m_fdlRe.begin(), m_fdlRe.end(), 0.0f);
    std::fill(m_fdlIm.begin(), m_fdlIm.end(), 0.0f);
    std::fill(m_accRe.begin(), m_accRe.end(), 0.0f);
    std::fill(m_accIm.begin(), m_accIm.end(), 0.0f);
    std::fill(m_tmp.begin(), m_tmp.end(), 0.0f);
    m_pos = 0;
    m_slotIndex = 0;
    m_fade = 0.0f;
    m_fadeStep = 0.0f;
    m_fadeTarget = (m_enabled && m_current) ? 1.0f : 0.0f;
    m_fade = m_fadeTarget;
}

void ConvolutionStage::setParams(const Convolution::Params &p) noexcept
{
    m_p = p;
    m_trim = dbToLin(clampTo(p.trimDb, -24.0f, 12.0f));
}

void ConvolutionStage::setEnabled(bool on) noexcept
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    m_fadeTarget = (on && m_current) ? 1.0f : 0.0f;
    m_fadeStep = (m_fadeTarget > m_fade) ? m_fadeInc : -m_fadeInc;
}

bool ConvolutionStage::offerKernel(ConvKernel *kernel) noexcept
{
    ConvKernel *expected = nullptr;
    return m_incoming.compare_exchange_strong(expected, kernel,
                                              std::memory_order_release,
                                              std::memory_order_relaxed);
}

ConvKernel *ConvolutionStage::reclaimKernel() noexcept
{
    return m_retired.exchange(nullptr, std::memory_order_acquire);
}

void ConvolutionStage::process(const AudioBuffer &buf) noexcept
{
    if (!buf.valid() || !m_armed || m_block == 0)
        return;

    const int channels = std::min(buf.channelCount, m_channels);
    const uint32_t block = m_block;

    for (int i = 0; i < buf.frames; ++i) {
        const float g = m_fade * clampTo(m_p.mix, 0.0f, 1.0f);

        for (int c = 0; c < channels; ++c) {
            const size_t base = size_t(c) * block;
            const float x = scrubNonFinite(buf.channels[c][i]);
            m_inAccum[base + m_pos] = x;

            // Read before write: exactly `block` samples of delay, matching
            // what the overlap-save path imposes on the wet signal. Keeping it
            // here rather than in the kernel is what stops a kernel swap from
            // ever discontinuing the dry path.
            const float dry = m_dryDelay[base + m_pos];
            m_dryDelay[base + m_pos] = x;

            const bool wetOn = m_current && m_current->route[c].spec != kConvBypass;
            const float wet = wetOn
                ? safeWet(m_current->wetGain * m_trim * m_outBuf[base + m_pos])
                : 0.0f;
            const float gc = wetOn ? g : 0.0f;

            // Linear, not equal-power. For a correction impulse response the
            // wet and dry signals are near-identical through the midband, so an
            // equal-power law would put a +3 dB swell in the middle of every
            // enable, disable and swap.
            buf.channels[c][i] = (1.0f - gc) * dry + gc * wet;
        }

        if (m_fade != m_fadeTarget) {
            m_fade += m_fadeStep;
            if ((m_fadeStep > 0.0f && m_fade > m_fadeTarget)
                || (m_fadeStep < 0.0f && m_fade < m_fadeTarget)) {
                m_fade = m_fadeTarget;
            }
        }

        if (++m_pos == block) {
            m_pos = 0;
            tick(channels);
        }
    }
}

void ConvolutionStage::tick(int channels) noexcept
{
    const uint32_t block = m_block;

    // --- kernel hand-off: three atomic operations, no loop, no delete -------
    if (!m_pending) {
        ConvKernel *incoming = m_incoming.exchange(nullptr, std::memory_order_acquire);
        if (incoming) {
            if (incoming->magic == kConvKernelMagic && incoming->block == block
                && incoming->partitions <= m_partitions
                && incoming->padBins == m_padBins) {
                // Duck to dry first, then install at the bottom of the fade, so
                // the swap itself is inaudible.
                m_pending = incoming;
                m_fadeTarget = 0.0f;
                m_fadeStep = -m_fadeInc;
            } else if (m_retired.load(std::memory_order_relaxed) == nullptr) {
                // Geometry changed under the builder. Hand it straight back.
                m_retired.store(incoming, std::memory_order_release);
            } else {
                m_incoming.store(incoming, std::memory_order_release);
            }
        }
    }
    if (m_pending && m_fade == 0.0f
        && m_retired.load(std::memory_order_relaxed) == nullptr) {
        ConvKernel *old = m_current;
        m_current = m_pending;
        m_pending = nullptr;
        if (old)
            m_retired.store(old, std::memory_order_release);
        m_fadeTarget = (m_enabled && m_current) ? 1.0f : 0.0f;
        m_fadeStep = m_fadeInc;
    }
    // A still-occupied `retired` simply means the builder has not collected
    // yet; the stage stays dry and tries again next tick. That is a bounded
    // wait for output quality, not a spin -- no extra work happens and the
    // function always returns.

    // --- slide the window, transform, publish into the delay line ----------
    m_slotIndex = (m_slotIndex + m_partitions - 1u) % m_partitions;
    for (int c = 0; c < channels; ++c) {
        float *win = m_win.data() + size_t(c) * 2 * block;
        std::memmove(win, win + block, sizeof(float) * block);
        std::memcpy(win + block, m_inAccum.data() + size_t(c) * block,
                    sizeof(float) * block);

        m_fft.realForward(win, m_cplx.data());
        float *re = fdlRe(c, m_slotIndex);
        float *im = fdlIm(c, m_slotIndex);
        for (uint32_t b = 0; b < m_bins; ++b) {
            re[b] = m_cplx[b].real();
            im[b] = m_cplx[b].imag();
        }
    }

    const ConvKernel *k = m_current;
    if (!k) {
        std::fill(m_outBuf.begin(), m_outBuf.end(), 0.0f);
        return;
    }

    // --- accumulate and transform back -------------------------------------
    const uint32_t partitions = k->partitions;
    for (int co = 0; co < channels; ++co) {
        const uint32_t spec = k->route[co].spec;
        float *out = m_outBuf.data() + size_t(co) * block;
        if (spec == kConvBypass) {
            std::memset(out, 0, sizeof(float) * block);
            continue;
        }
        const int ci = int(k->route[co].inCh);

        std::memset(m_accRe.data(), 0, sizeof(float) * m_padBins);
        std::memset(m_accIm.data(), 0, sizeof(float) * m_padBins);

        for (uint32_t p = 0; p < partitions; ++p) {
            // slotIndex < partitions and p < partitions, so one conditional
            // subtract replaces a modulo.
            uint32_t s = m_slotIndex + p;
            if (s >= m_partitions)
                s -= m_partitions;

            const float *xr = fdlRe(ci, s);
            const float *xi = fdlIm(ci, s);
            const float *hr = k->re + (size_t(spec) * partitions + p) * m_padBins;
            const float *hi = k->im + (size_t(spec) * partitions + p) * m_padBins;

            for (uint32_t b = 0; b < m_padBins; ++b) {
                m_accRe[b] += xr[b] * hr[b] - xi[b] * hi[b];
                m_accIm[b] += xr[b] * hi[b] + xi[b] * hr[b];
            }
        }

        for (uint32_t b = 0; b < m_bins; ++b)
            m_cplx[b] = std::complex<float>(m_accRe[b], m_accIm[b]);
        m_fft.realInverse(m_cplx.data(), m_tmp.data());

        // Overlap-save: the second half is the linear convolution, the first
        // half is circular wraparound and is discarded.
        std::memcpy(out, m_tmp.data() + block, sizeof(float) * block);
    }
}

} // namespace dreamdsp::dsp
