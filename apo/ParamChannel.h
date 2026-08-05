#pragma once

#include <windows.h>

#include <atomic>

#include "Convolver.h"
#include "ImpulseBlob.h"
#include "ParamSlots.h"
#include "StatusBlock.h"

namespace dreamdsp::apo {

class DreamApo;

// The audiodg-side end of the parameter channel.
//
// One instance per process, not one per APO: a machine with several endpoints
// attached runs several DreamApo objects inside the same audiodg, and they all
// want the same parameters. Watching one file once and publishing into one slot
// buffer costs one thread instead of eight.
//
// Everything that can block -- waiting for a change notification, opening the
// file, reading it -- happens on the watcher thread. The audio callback only
// ever performs an atomic load and a 256-byte copy.
class ParamChannel
{
public:
    // Constant-initialised namespace-scope object, so there is no dynamic
    // initialiser and therefore nothing running under the loader lock.
    static ParamChannel &instance() noexcept;

    // Called from LockForProcess / UnlockForProcess, i.e. a normal thread.
    // Starts the watcher on the first acquire and stops it after the last
    // release. Returns false if the channel could not be started, in which case
    // the caller must not call release().
    bool acquire(DreamApo *owner) noexcept;
    void release(DreamApo *owner) noexcept;

    // Read from the audio callback. Real-time safe.
    const dsp::ParamSlots &slots() const noexcept { return m_slots; }

private:
    static DWORD WINAPI watcherEntry(void *self) noexcept;
    void watch() noexcept;
    void reloadIfChanged() noexcept;
    void rebuildImpulseResponses(const dsp::ParamBlock &block) noexcept;
    bool loadImpulseBlob(const dsp::Convolution::Params &p) noexcept;
    void maybeWriteStatus() noexcept;
    void rearmNotify() noexcept;
    void closeNotify() noexcept;

    dsp::ParamSlots m_slots;

    // Guards start-up and shutdown only. Never taken by the audio callback.
    SRWLOCK m_lock = SRWLOCK_INIT;
    int m_refCount = 0;
    bool m_dead = false;         // a watcher that would not stop; never restart
    bool m_everStarted = false;

    HANDLE m_thread = nullptr;
    HANDLE m_stop = nullptr;
    HANDLE m_notify = INVALID_HANDLE_VALUE;

    // Fixed-size registration. Eight endpoints is already more than any real
    // machine streams at once, and a fixed array means no allocation and no
    // container to reason about from two threads.
    static constexpr int kMaxOwners = 8;
    DreamApo *m_owners[kMaxOwners] = {};

    // What each instance was last given, so a kernel is rebuilt only when the
    // impulse response actually changed -- not on every parameter write.
    struct BuiltState {
        uint8_t hash[16] = {};
        uint32_t rate = 0;
        uint32_t channels = 0;
        bool valid = false;
    };
    BuiltState m_built[kMaxOwners];

    // The impulse response as it came off disk: planar, at its own rate. Held
    // once for the whole process rather than per instance, because two
    // endpoints running at different rates still start from the same file.
    std::vector<float> m_irSamples;
    dsp::IrBlobHeader m_irHeader{};
    uint8_t m_irHash[16] = {};
    bool m_irLoaded = false;
    uint32_t m_irStatus = 0;      // dsp::IrBlobError of the last attempt

    // Change detection, so an unchanged file costs one stat and nothing else.
    DWORD m_stampLow = 0;
    DWORD m_stampHigh = 0;
    DWORD m_stampSize = 0;

    std::atomic<uint32_t> m_loadedGeneration{ 0 };
    std::atomic<uint32_t> m_loadCount{ 0 };
    std::atomic<uint32_t> m_rejectCount{ 0 };
    std::atomic<uint32_t> m_lastError{ 0 };

    ULONGLONG m_lastStatusTick = 0;
    dsp::StatusBlock m_lastStatus{};
};

} // namespace dreamdsp::apo
