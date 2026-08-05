#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dreamdsp::dsp {

// The on-disk form of an impulse response, as it travels from the GUI to the
// copy of the DSP inside audiodg.exe.
//
// A separate file rather than a field in ParamBlock, because an impulse
// response is data, not a scalar: five seconds of stereo at 96 kHz is nearly
// four megabytes. ParamBlock carries only its identity -- a content hash -- and
// the file is named after that hash, so a blob and the parameters that
// reference it can never be mismatched even if they are written out of order.
//
// Samples are stored PLANAR and at the file's own rate. Conversion to the
// endpoint's rate happens inside the APO, because only the APO knows what that
// rate currently is; doing it in the GUI would just relocate the silent
// sample-rate mismatch this whole feature exists to remove.

enum : uint32_t {
    kIrBlobMagic = 0x52494244u,   // reads as DBIR
    kIrBlobVersion = 1u
};

struct IrBlobHeader {
    uint32_t magic;         // +  0
    uint32_t version;       // +  4
    uint32_t headerBytes;   // +  8   payload starts here
    uint32_t sampleRate;    // + 12   the file's own rate, not the device's
    uint32_t frames;        // + 16   per channel
    uint32_t channels;      // + 20
    uint32_t flags;         // + 24
    uint32_t reserved;      // + 28
    uint8_t hash[16];       // + 32   of the sample payload alone
    uint32_t pad[4];        // + 48
};                          // = 64

static_assert(sizeof(IrBlobHeader) == 64, "blob header layout changed");
static_assert(offsetof(IrBlobHeader, hash) == 32, "blob header layout changed");
static_assert(std::is_trivially_copyable<IrBlobHeader>::value, "must be memcpy-able");

// A 128-bit content hash.
//
// For identity, not for security -- the file lives somewhere the user can write
// and there is no adversary this could defend against. What it has to do is
// make "the parameters name a different impulse response than the one on disk"
// detectable, including when the two were written in the wrong order or one of
// them is a leftover from a previous run.
void irHash128(const void *data, size_t bytes, uint8_t out[16]) noexcept;

// Fills in a header for `samples` (planar, channels * frames floats) and
// computes its hash. Does not write anything.
IrBlobHeader makeIrBlobHeader(const float *samples, uint32_t frames,
                              uint32_t channels, uint32_t sampleRate) noexcept;

// Checks a buffer that is supposed to hold a whole blob.
//
// Everything is bounded before anything is trusted: the header fields are
// checked against the limits, the declared size against the actual size, and
// the payload against its own hash. Returns null on any failure, with `why`
// set to a short reason.
enum class IrBlobError : uint32_t {
    None = 0,
    TooSmall,
    BadMagic,
    BadVersion,
    BadGeometry,
    SizeMismatch,
    HashMismatch
};

// Returns a pointer to the planar samples inside `data`, or null.
const float *validateIrBlob(const void *data, size_t bytes,
                            IrBlobHeader *headerOut, IrBlobError *why) noexcept;

} // namespace dreamdsp::dsp
