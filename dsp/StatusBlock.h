#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dreamdsp::dsp {

// What the APO reports back about itself.
//
// The parameter channel is one-way by design, but a one-way channel makes every
// failure look identical from the outside: "I moved a slider and nothing
// happened" could be a rejected file, an endpoint we are not attached to, an
// audiodg that never loaded us, or a chain that is running exactly as asked.
// This block is what tells them apart, and it is why the GUI can say which one
// it is instead of the user having to guess.
enum : uint32_t {
    kStatusMagic = 0x53505244u,   // reads as DRPS
    kStatusVersion = 1u
};

enum : uint32_t {
    kSfStreaming   = 1u << 0,   // between LockForProcess and UnlockForProcess
    kSfPassthrough = 1u << 1,   // enableMask == 0, taking the memcpy path
    kSfInPlace     = 1u << 2    // the engine handed us dst == src
};

struct StatusInstance {          // 32 bytes
    uint32_t sampleRate;         // +  0
    uint32_t channels;           // +  4
    uint32_t maxFrames;          // +  8
    uint32_t appliedGeneration;  // + 12   last block actually installed
    uint32_t appliedMask;        // + 16   what it is really running
    uint32_t flags;              // + 20   kSf*
    uint64_t framesProcessed;    // + 24
};

struct StatusBlock {             // 296 bytes
    uint32_t magic;              // +  0
    uint32_t version;            // +  4
    uint32_t sizeBytes;          // +  8
    uint32_t loadedGeneration;   // + 12   generation the loader last accepted
    uint32_t loadCount;          // + 16   successful loads since the DLL loaded
    uint32_t rejectCount;        // + 20   reads that failed validation
    uint32_t lastError;          // + 24   GetLastError of the last failed access
    uint32_t instanceCount;      // + 28   0..8
    uint64_t apoTickMs;          // + 32   GetTickCount64 at the time of writing
    StatusInstance inst[8];      // + 40   256
};

static_assert(sizeof(StatusInstance) == 32, "status layout changed");
static_assert(sizeof(StatusBlock) == 296, "status layout changed");
static_assert(offsetof(StatusBlock, inst) == 40, "status layout changed");
static_assert(std::is_trivially_copyable<StatusBlock>::value, "must be memcpy-able");

} // namespace dreamdsp::dsp
