#include "ParamChannel.h"
#include "DreamApo.h"

#include <cstring>

namespace dreamdsp::apo {

namespace {

const wchar_t *kControlDir = L"C:\\ProgramData\\DreamDSP\\control";
const wchar_t *kParamPath  = L"C:\\ProgramData\\DreamDSP\\control\\params.bin";
const wchar_t *kStatusPath = L"C:\\ProgramData\\DreamDSP\\status.bin";
const wchar_t *kStatusTemp = L"C:\\ProgramData\\DreamDSP\\status.tmp";

// Backstop poll. The change notification is the fast path; this is what keeps
// the channel working if the notification handle is lost -- on a network path,
// after a directory is recreated, or if the handle simply fails to re-arm.
constexpr DWORD kPollMs = 250;

// QSaveFile publishes with a create followed by a rename, which fires two
// notifications. Absorbing the second turns one publish into one file open
// rather than two.
constexpr DWORD kCoalesceMs = 15;

constexpr ULONGLONG kStatusIntervalMs = 1000;

// Constant-initialised, at namespace scope, deliberately. A function-local
// static would emit a guard variable and run its constructor lazily -- under
// the loader lock if the first call ever happened during DLL initialisation.
// Every member here is either zero or SRWLOCK_INIT, so there is no constructor
// to run at all.
ParamChannel g_channel;

} // namespace

ParamChannel &ParamChannel::instance() noexcept
{
    return g_channel;
}

bool ParamChannel::acquire(DreamApo *owner) noexcept
{
    ::AcquireSRWLockExclusive(&m_lock);

    if (m_dead) {
        ::ReleaseSRWLockExclusive(&m_lock);
        return false;
    }

    bool registered = false;
    for (int i = 0; i < kMaxOwners; ++i) {
        if (!m_owners[i]) {
            m_owners[i] = owner;
            registered = true;
            break;
        }
    }
    if (!registered) {
        // More concurrent streams than the status block can describe. Running
        // without parameters is better than failing to stream.
        ::ReleaseSRWLockExclusive(&m_lock);
        return false;
    }

    if (m_refCount++ == 0) {
        if (!m_everStarted) {
            // Transparent until proven otherwise: a zeroed slot has
            // enableMask == 0, so a chain that never receives a block passes
            // audio through untouched.
            m_slots.clear();
            m_everStarted = true;

            // Pin the module. The watcher thread runs code in this DLL, and
            // release() is allowed to give up waiting for it; pinning means an
            // abandoned thread can never be executing in unmapped memory.
            HMODULE self = nullptr;
            ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                     | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                 reinterpret_cast<LPCWSTR>(&ParamChannel::instance),
                                 &self);
        }

        ::CreateDirectoryW(L"C:\\ProgramData\\DreamDSP", nullptr);
        ::CreateDirectoryW(kControlDir, nullptr);

        m_stop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (m_stop) {
            rearmNotify();
            m_thread = ::CreateThread(nullptr, 0, &ParamChannel::watcherEntry, this, 0, nullptr);
        }
        if (!m_thread) {
            // No watcher means no parameter updates, but streaming must still
            // work -- the slots hold the transparent block.
            trace(L"param channel failed to start", ::GetLastError());
        }
    }

    ::ReleaseSRWLockExclusive(&m_lock);
    return true;
}

void ParamChannel::release(DreamApo *owner) noexcept
{
    ::AcquireSRWLockExclusive(&m_lock);

    for (int i = 0; i < kMaxOwners; ++i) {
        if (m_owners[i] == owner) {
            m_owners[i] = nullptr;
            break;
        }
    }

    if (m_refCount > 0 && --m_refCount == 0 && m_thread) {
        ::SetEvent(m_stop);
        // Bounded. UnlockForProcess runs while the audio engine is tearing a
        // stream down and must not be held up indefinitely by a thread stuck in
        // a filesystem call; the module is pinned, so abandoning it is safe.
        if (::WaitForSingleObject(m_thread, 5000) != WAIT_OBJECT_0) {
            m_dead = true;
            trace(L"param channel watcher would not stop; abandoned");
        }
        ::CloseHandle(m_thread);
        m_thread = nullptr;
        ::CloseHandle(m_stop);
        m_stop = nullptr;
        closeNotify();
    }

    ::ReleaseSRWLockExclusive(&m_lock);
}

void ParamChannel::rearmNotify() noexcept
{
    if (m_notify != INVALID_HANDLE_VALUE)
        return;
    m_notify = ::FindFirstChangeNotificationW(kControlDir, FALSE,
                                              FILE_NOTIFY_CHANGE_LAST_WRITE
                                                  | FILE_NOTIFY_CHANGE_FILE_NAME);
}

void ParamChannel::closeNotify() noexcept
{
    if (m_notify != INVALID_HANDLE_VALUE) {
        ::FindCloseChangeNotification(m_notify);
        m_notify = INVALID_HANDLE_VALUE;
    }
}

DWORD WINAPI ParamChannel::watcherEntry(void *self) noexcept
{
    static_cast<ParamChannel *>(self)->watch();
    return 0;
}

void ParamChannel::watch() noexcept
{
    // Pick up whatever is already on disk before waiting for a change, so an
    // audiodg that restarts hours after the GUI last wrote still comes up with
    // the user's settings audible on the first block.
    reloadIfChanged();

    for (;;) {
        HANDLE handles[2] = { m_stop, m_notify };
        const DWORD count = (m_notify != INVALID_HANDLE_VALUE) ? 2u : 1u;
        const DWORD r = ::WaitForMultipleObjects(count, handles, FALSE, kPollMs);

        if (r == WAIT_OBJECT_0)
            break;                                  // the stop event, the only exit

        if (count == 2 && r == WAIT_OBJECT_0 + 1) {
            ::FindNextChangeNotification(m_notify);
            ::WaitForSingleObject(m_notify, kCoalesceMs);
            ::FindNextChangeNotification(m_notify);
        } else if (r == WAIT_FAILED) {
            // Never exit on an error. Drop the handle and fall back to plain
            // polling; the next pass tries to re-arm.
            closeNotify();
        }

        reloadIfChanged();
        maybeWriteStatus();

        if (m_notify == INVALID_HANDLE_VALUE)
            rearmNotify();
    }
}

void ParamChannel::reloadIfChanged() noexcept
{
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (!::GetFileAttributesExW(kParamPath, GetFileExInfoStandard, &info)) {
        m_lastError.store(::GetLastError(), std::memory_order_relaxed);
        return;                                     // keep the last good block
    }
    if (info.ftLastWriteTime.dwLowDateTime == m_stampLow
        && info.ftLastWriteTime.dwHighDateTime == m_stampHigh
        && info.nFileSizeLow == m_stampSize) {
        return;                                     // unchanged: one syscall
    }

    // FILE_SHARE_DELETE is load-bearing, and is why there is no retry loop
    // here: a rename landing while this handle is open unlinks the old file
    // object rather than failing, we finish reading the old bytes, and the next
    // pass picks up the new ones.
    HANDLE file = ::CreateFileW(kParamPath, GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        m_lastError.store(::GetLastError(), std::memory_order_relaxed);
        return;
    }

    dsp::ParamBlock raw;
    DWORD got = 0;
    const BOOL ok = ::ReadFile(file, &raw, DWORD(sizeof raw), &got, nullptr);
    ::CloseHandle(file);

    if (!ok || got != sizeof raw) {
        m_rejectCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (raw.magic != dsp::kParamMagic || raw.version != dsp::kParamVersion
        || raw.sizeBytes != sizeof raw) {
        m_rejectCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // A block that fails validation never replaces a good one. Missing,
    // truncated, foreign or version-skewed all leave the previous parameters in
    // place -- reverting to silence or to defaults on a bad read would be a far
    // more startling failure than simply not changing.
    m_slots.publish(dsp::sanitise(raw));

    m_loadedGeneration.store(raw.generation, std::memory_order_relaxed);
    m_loadCount.fetch_add(1, std::memory_order_relaxed);
    m_lastError.store(0, std::memory_order_relaxed);

    m_stampLow = info.ftLastWriteTime.dwLowDateTime;
    m_stampHigh = info.ftLastWriteTime.dwHighDateTime;
    m_stampSize = info.nFileSizeLow;

    trace(L"params loaded gen/mask/count", raw.generation, raw.enableMask,
          m_loadCount.load(std::memory_order_relaxed));
}

void ParamChannel::maybeWriteStatus() noexcept
{
    const ULONGLONG now = ::GetTickCount64();
    if (now - m_lastStatusTick < kStatusIntervalMs)
        return;
    m_lastStatusTick = now;

    dsp::StatusBlock s;
    std::memset(&s, 0, sizeof s);
    s.magic = dsp::kStatusMagic;
    s.version = dsp::kStatusVersion;
    s.sizeBytes = uint32_t(sizeof s);
    s.loadedGeneration = m_loadedGeneration.load(std::memory_order_relaxed);
    s.loadCount = m_loadCount.load(std::memory_order_relaxed);
    s.rejectCount = m_rejectCount.load(std::memory_order_relaxed);
    s.lastError = m_lastError.load(std::memory_order_relaxed);
    s.apoTickMs = now;

    // Reading the owner table without the lock: acquire/release only ever write
    // a whole pointer, the array is fixed, and a torn read is impossible for a
    // pointer-sized aligned slot. Taking the lock here would put a filesystem
    // write inside the same lock LockForProcess uses.
    uint32_t n = 0;
    for (int i = 0; i < kMaxOwners && n < 8; ++i) {
        DreamApo *owner = m_owners[i];
        if (!owner)
            continue;
        owner->fillStatus(s.inst[n]);
        ++n;
    }
    s.instanceCount = n;

    // Skip the write when nothing changed, so a machine sitting idle is not
    // touching the disk once a second forever. apoTickMs is excluded from the
    // comparison for exactly that reason.
    dsp::StatusBlock previous = m_lastStatus;
    previous.apoTickMs = s.apoTickMs;
    if (std::memcmp(&previous, &s, sizeof s) == 0)
        return;
    m_lastStatus = s;

    HANDLE file = ::CreateFileW(kStatusTemp, GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    ::WriteFile(file, &s, DWORD(sizeof s), &written, nullptr);
    ::CloseHandle(file);
    if (written == sizeof s) {
        // Into the parent directory, not the watched one: writing it beside
        // params.bin would wake this very watcher with its own output.
        ::MoveFileExW(kStatusTemp, kStatusPath, MOVEFILE_REPLACE_EXISTING);
    }
}

} // namespace dreamdsp::apo
