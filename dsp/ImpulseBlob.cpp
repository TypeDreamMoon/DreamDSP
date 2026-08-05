#include "ImpulseBlob.h"

#include "Convolver.h"   // kIrMax* limits

#include <cstring>

namespace dreamdsp::dsp {

namespace {

// Two independent FNV-1a passes, one forward and one over the bytes read in
// reverse, with different offset bases. Deliberately simple and written out
// rather than vendored: this needs to distinguish files, not resist an attack,
// and a hash nobody has to go and look up is worth more here than a better one.
constexpr uint64_t kFnvOffsetA = 0xcbf29ce484222325ull;
constexpr uint64_t kFnvOffsetB = 0x9e3779b97f4a7c15ull;
constexpr uint64_t kFnvPrime = 0x100000001b3ull;

} // namespace

void irHash128(const void *data, size_t bytes, uint8_t out[16]) noexcept
{
    const auto *p = static_cast<const uint8_t *>(data);

    uint64_t a = kFnvOffsetA ^ uint64_t(bytes);
    uint64_t b = kFnvOffsetB ^ (uint64_t(bytes) << 32);

    for (size_t i = 0; i < bytes; ++i) {
        a = (a ^ p[i]) * kFnvPrime;
        b = (b ^ p[bytes - 1 - i]) * kFnvPrime;
    }

    // Final avalanche, so a one-byte change moves every output bit rather than
    // only the low ones.
    a ^= a >> 33; a *= 0xff51afd7ed558ccdull; a ^= a >> 33;
    b ^= b >> 29; b *= 0xc4ceb9fe1a85ec53ull; b ^= b >> 32;

    std::memcpy(out, &a, 8);
    std::memcpy(out + 8, &b, 8);
}

IrBlobHeader makeIrBlobHeader(const float *samples, uint32_t frames,
                              uint32_t channels, uint32_t sampleRate) noexcept
{
    IrBlobHeader h;
    std::memset(&h, 0, sizeof h);
    h.magic = kIrBlobMagic;
    h.version = kIrBlobVersion;
    h.headerBytes = uint32_t(sizeof(IrBlobHeader));
    h.sampleRate = sampleRate;
    h.frames = frames;
    h.channels = channels;
    if (samples && frames > 0 && channels > 0)
        irHash128(samples, size_t(frames) * channels * sizeof(float), h.hash);
    return h;
}

const float *validateIrBlob(const void *data, size_t bytes,
                            IrBlobHeader *headerOut, IrBlobError *why) noexcept
{
    const auto fail = [&](IrBlobError e) -> const float * {
        if (why)
            *why = e;
        return nullptr;
    };
    if (why)
        *why = IrBlobError::None;

    if (!data || bytes < sizeof(IrBlobHeader))
        return fail(IrBlobError::TooSmall);

    IrBlobHeader h;
    std::memcpy(&h, data, sizeof h);
    if (headerOut)
        *headerOut = h;

    if (h.magic != kIrBlobMagic)
        return fail(IrBlobError::BadMagic);
    if (h.version != kIrBlobVersion || h.headerBytes != sizeof(IrBlobHeader))
        return fail(IrBlobError::BadVersion);

    // Every bound is checked before a single multiplication that could overflow.
    if (h.frames == 0 || h.frames > kIrMaxFrames)
        return fail(IrBlobError::BadGeometry);
    if (h.channels == 0 || h.channels > kConvMaxChannels)
        return fail(IrBlobError::BadGeometry);
    if (h.sampleRate < 8000u || h.sampleRate > 384000u)
        return fail(IrBlobError::BadGeometry);

    const uint64_t samples = uint64_t(h.frames) * h.channels;
    if (samples > kIrMaxSamples)
        return fail(IrBlobError::BadGeometry);

    const uint64_t payload = samples * sizeof(float);
    if (payload + sizeof(IrBlobHeader) != uint64_t(bytes))
        return fail(IrBlobError::SizeMismatch);

    const auto *floats = reinterpret_cast<const float *>(
        static_cast<const uint8_t *>(data) + sizeof(IrBlobHeader));

    uint8_t computed[16];
    irHash128(floats, size_t(payload), computed);
    if (std::memcmp(computed, h.hash, 16) != 0)
        return fail(IrBlobError::HashMismatch);

    return floats;
}

} // namespace dreamdsp::dsp
