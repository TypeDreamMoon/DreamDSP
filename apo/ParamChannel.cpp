#include "ParamChannel.h"
#include "DreamApo.h"

#include "Resampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

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

// The frequency-delay line is sized for this in EffectChain::prepare, so the
// two have to agree. 64 partitions is 340 ms at 48 kHz and 170 ms at 96 kHz --
// enough for room, headphone and HRTF correction, which is what convolution is
// mostly used for; a long reverb tail is truncated rather than refused.
constexpr uint32_t kMaxPartitions = 64;

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
            // Load synchronously before the watcher starts. LockForProcess runs
            // on a normal thread and is allowed to take the time, and doing it
            // here is what lets the caller find out -- immediately, rather than
            // up to a poll interval later -- whether an impulse response is
            // configured, which decides the latency it reports to Windows.
            reloadIfChanged();
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
    const dsp::ParamBlock clean = dsp::sanitise(raw);
    m_slots.publish(clean);

    // Building a convolution kernel means reading a file, resampling it, and
    // running a few hundred transforms -- tens to hundreds of milliseconds.
    //
    // It happens here, on the watcher thread, rather than on a thread of its
    // own. The cost is that the next parameter update is delayed by one build,
    // once, right after the user picks a different impulse response. The
    // benefit is one fewer thread and one fewer synchronisation problem inside
    // audiodg. Neither arrangement touches the audio thread.
    rebuildImpulseResponses(clean);

    m_loadedGeneration.store(raw.generation, std::memory_order_relaxed);
    m_loadCount.fetch_add(1, std::memory_order_relaxed);
    m_lastError.store(0, std::memory_order_relaxed);

    m_stampLow = info.ftLastWriteTime.dwLowDateTime;
    m_stampHigh = info.ftLastWriteTime.dwHighDateTime;
    m_stampSize = info.nFileSizeLow;

    trace(L"params loaded gen/mask/count", raw.generation, raw.enableMask,
          m_loadCount.load(std::memory_order_relaxed));
}

// Reads the blob the parameters name. True when m_irSamples holds it.
bool ParamChannel::loadImpulseBlob(const dsp::Convolution::Params &p) noexcept
{
    if (m_irLoaded && std::memcmp(m_irHash, p.irHash, 16) == 0)
        return true;

    m_irLoaded = false;
    m_irSamples.clear();

    // Content-addressed: the file is named after the hash of its own samples,
    // so the parameters and the payload cannot be mismatched even though they
    // are two separate writes with no atomicity between them.
    wchar_t path[MAX_PATH];
    int at = ::swprintf(path, MAX_PATH, L"C:\\ProgramData\\DreamDSP\\control\\ir\\");
    if (at <= 0)
        return false;
    for (int i = 0; i < 16 && at < MAX_PATH - 8; ++i)
        at += ::swprintf(path + at, size_t(MAX_PATH - at), L"%02x", p.irHash[i]);
    if (at >= MAX_PATH - 8)
        return false;
    ::swprintf(path + at, size_t(MAX_PATH - at), L".irb");

    HANDLE file = ::CreateFileW(path, GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        m_irStatus = uint32_t(dsp::IrBlobError::TooSmall);
        return false;
    }

    LARGE_INTEGER size = {};
    if (!::GetFileSizeEx(file, &size) || size.QuadPart <= 0
        || size.QuadPart > LONGLONG(dsp::kIrMaxBytes)) {
        ::CloseHandle(file);
        m_irStatus = uint32_t(dsp::IrBlobError::BadGeometry);
        return false;
    }

    std::vector<unsigned char> raw;
    raw.resize(size_t(size.QuadPart));
    DWORD got = 0;
    const BOOL ok = ::ReadFile(file, raw.data(), DWORD(raw.size()), &got, nullptr);
    ::CloseHandle(file);
    if (!ok || got != raw.size()) {
        m_irStatus = uint32_t(dsp::IrBlobError::SizeMismatch);
        return false;
    }

    dsp::IrBlobError why = dsp::IrBlobError::None;
    const float *samples = dsp::validateIrBlob(raw.data(), raw.size(), &m_irHeader, &why);
    m_irStatus = uint32_t(why);
    if (!samples) {
        trace(L"impulse response rejected, reason", uint32_t(why));
        return false;
    }

    const size_t count = size_t(m_irHeader.frames) * m_irHeader.channels;
    m_irSamples.assign(samples, samples + count);
    std::memcpy(m_irHash, p.irHash, 16);
    m_irLoaded = true;
    trace(L"impulse response loaded frames/ch/rate",
          m_irHeader.frames, m_irHeader.channels, m_irHeader.sampleRate);
    return true;
}

void ParamChannel::rebuildImpulseResponses(const dsp::ParamBlock &block) noexcept
{
    const dsp::Convolution::Params &p = block.convolution;

    for (int i = 0; i < kMaxOwners; ++i) {
        DreamApo *owner = m_owners[i];
        if (!owner)
            continue;

        // Whatever the audio thread has finished with is freed here, on this
        // thread, never on the callback.
        dsp::destroyConvKernel(owner->chain().convolution().reclaimKernel());

        if (p.irGeneration == 0u) {
            m_built[i] = BuiltState{};
            continue;
        }

        const uint32_t rate = uint32_t(owner->streamRate());
        const uint32_t channels = uint32_t(owner->streamChannels());
        if (rate == 0 || channels == 0)
            continue;

        if (m_built[i].valid && m_built[i].rate == rate
            && m_built[i].channels == channels
            && std::memcmp(m_built[i].hash, p.irHash, 16) == 0) {
            continue;                          // already running exactly this
        }

        if (!loadImpulseBlob(p))
            continue;

        // Resampled to THIS instance's rate. Two endpoints can be running at
        // different rates from the same file, which is the whole reason the
        // conversion happens here rather than in the GUI: only the APO knows
        // what rate each stream is actually at, right now.
        const uint32_t irFrames = m_irHeader.frames;
        const uint32_t irChannels = m_irHeader.channels;
        const double srcRate = double(m_irHeader.sampleRate);

        // Declared and then resized, rather than constructed with arguments.
        // `converted(size_t(irChannels), std::vector<float>())` looks like a
        // constructor call and is not: both arguments parse as parameter
        // declarations, so the whole line becomes a function declaration.
        std::vector<std::vector<float>> converted;
        converted.resize(size_t(irChannels));
        std::vector<const float *> planes(size_t(irChannels), nullptr);
        bool ready = true;

        for (uint32_t c = 0; c < irChannels; ++c) {
            const float *src = m_irSamples.data() + size_t(c) * irFrames;
            converted[c] = dsp::Resampler::resample(src, int(irFrames), srcRate, double(rate));
            if (converted[c].empty()) {
                ready = false;
                break;
            }
            // A resampler preserves sample values, which is correct for a
            // signal and wrong for a filter: the sum of the taps -- the
            // filter's DC gain -- scales with the rate ratio. Compensating
            // here rather than inside the resampler keeps its signal behaviour,
            // which the offline renderer and the tests depend on.
            const float scale = float(srcRate / double(rate));
            for (auto &v : converted[c])
                v *= scale;
            planes[c] = converted[c].data();
        }
        if (!ready)
            continue;

        int taps = int(converted[0].size());

        // Too long for the budget? Shorten it rather than refusing it.
        //
        // A user who points at a six-second cathedral gets a shorter
        // cathedral, which is audibly what they asked for. Refusing gives them
        // silence and a message, which is not.
        //
        // The cut is faded out over its last few milliseconds with a raised
        // cosine. A hard cut leaves a step in the impulse response, and a step
        // is broadband -- it would spray a click across the whole spectrum on
        // every transient.
        const uint32_t block = dsp::convBlockForRate(double(rate));
        const int maxTaps = int(kMaxPartitions * block);
        if (taps > maxTaps) {
            const int fade = std::min(int(0.005 * double(rate)), maxTaps / 10);
            for (uint32_t c = 0; c < irChannels; ++c) {
                converted[c].resize(size_t(maxTaps));
                for (int i = 0; i < fade; ++i) {
                    const double t = double(i) / double(fade > 1 ? fade - 1 : 1);
                    const float w = float(0.5 * (1.0 + std::cos(3.14159265358979323846 * t)));
                    converted[c][size_t(maxTaps - fade + i)] *= w;
                }
                planes[c] = converted[c].data();
            }
            trace(L"impulse response truncated taps/to/fade",
                  uint32_t(taps), uint32_t(maxTaps), uint32_t(fade));
            taps = maxTaps;
        }

        // Normalised by the energy of the response, so a random file from the
        // internet cannot arrive 30 dB hot. For a system-wide effect where the
        // listener is not mixing, "cannot make anything much louder" is the
        // right default.
        double energy = 0.0;
        for (uint32_t c = 0; c < irChannels; ++c) {
            double sum = 0.0;
            for (float v : converted[c])
                sum += double(v) * double(v);
            energy = std::max(energy, sum);
        }
        const float wetGain = (energy > 1e-12) ? float(1.0 / std::sqrt(energy)) : 1.0f;

        dsp::ConvBuildRequest req;
        req.irChannels = planes.data();
        req.irChannelCount = int(irChannels);
        req.tapCount = taps;
        req.streamChannels = int(channels);
        req.block = block;
        req.flags = p.flags;
        req.channelMask = owner->channelMask();
        req.wetGain = wetGain;
        req.maxPartitions = kMaxPartitions;

        dsp::ConvKernel *kernel = dsp::buildConvKernel(req);
        if (!kernel) {
            trace(L"impulse response does not fit the budget, taps", uint32_t(taps));
            continue;
        }

        if (!owner->chain().convolution().offerKernel(kernel)) {
            // A hand-off is still in flight. Drop this one and try again next
            // pass rather than building a queue.
            dsp::destroyConvKernel(kernel);
            continue;
        }

        std::memcpy(m_built[i].hash, p.irHash, 16);
        m_built[i].rate = rate;
        m_built[i].channels = channels;
        m_built[i].valid = true;
        trace(L"convolution kernel built taps/partitions/rate",
              uint32_t(taps), kernel->partitions, rate);
    }
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
