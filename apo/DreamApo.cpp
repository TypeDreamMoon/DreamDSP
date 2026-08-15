#include "DreamApo.h"
#include "ParamChannel.h"

#include "Denormals.h"

#include <new>
#include <cstdio>

namespace dreamdsp::apo {

// {6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E10}
const CLSID CLSID_DreamDspApo =
    { 0x6d2f1c55, 0x5e4b, 0x4a7e, { 0x9c, 0x31, 0x0d, 0x5a, 0x6c, 0x4b, 0x7e, 0x10 } };

// Diagnostics. There is no debugger and no console inside audiodg.exe, so the
// only way to know whether the APO was loaded at all -- and with what format --
// is to leave a trace. Never called from APOProcess: this opens a file.
void trace(const wchar_t *what, unsigned a, unsigned b, unsigned c)
{
    HANDLE h = ::CreateFileW(L"C:\\ProgramData\\DreamDSP\\apo.log",
                             FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;

    SYSTEMTIME t{};
    ::GetLocalTime(&t);

    char line[256];
    const int n = ::_snprintf_s(line, sizeof(line), _TRUNCATE,
                                "%02u:%02u:%02u.%03u  %ls  %u %u %u\r\n",
                                t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                                what, a, b, c);
    if (n > 0) {
        DWORD written = 0;
        ::WriteFile(h, line, DWORD(n), &written, nullptr);
    }
    ::CloseHandle(h);
}

namespace {

// Registration properties are handed to the audio engine as a plain struct.
// Static so nothing is allocated when the engine asks for them.
APO_REG_PROPERTIES g_regProperties = {
    /* clsid              */ { 0x6d2f1c55, 0x5e4b, 0x4a7e, { 0x9c, 0x31, 0x0d, 0x5a, 0x6c, 0x4b, 0x7e, 0x10 } },
    // INPLACE says the engine may hand us the same buffer for input and output.
    // APOProcess reads all of the input into its own deinterleaved scratch
    // before writing a single output sample, so that is safe -- but every
    // pass-through path has to check for it, because memcpy with identical
    // pointers is undefined behaviour.
    //
    // SAMPLESPERFRAME_MUST_MATCH (part of APO_FLAG_DEFAULT) is deliberately not
    // set: it would refuse formats whose channel count merely differs between
    // input and output, which is not the constraint we have. The real
    // constraint is 1..8 channels, and formatAcceptable enforces it directly.
    /* Flags              */ static_cast<APO_FLAG>(APO_FLAG_FRAMESPERSECOND_MUST_MATCH
                                                   | APO_FLAG_BITSPERSAMPLE_MUST_MATCH
                                                   | APO_FLAG_INPLACE),
    /* szFriendlyName     */ L"DreamDSP Effects",
    /* szCopyrightInfo    */ L"DreamDSP",
    /* u32MajorVersion    */ 0,
    /* u32MinorVersion    */ 1,
    /* u32MinInputConnections   */ 1,
    /* u32MaxInputConnections   */ 1,
    /* u32MinOutputConnections  */ 1,
    /* u32MaxOutputConnections  */ 1,
    // Unlimited. Zero here does not mean "no limit", it means no instance may
    // ever be created -- every APO on the system uses 0xFFFFFFFF.
    /* u32MaxInstances    */ 0xFFFFFFFFu,
    /* u32NumAPOInterfaces*/ 1,
    /* iidAPOInterfaceList*/ { __uuidof(IAudioProcessingObject) },
};

} // namespace

// Traced from the constructor onwards, not just from LockForProcess. An APO
// that is instantiated and then rejected -- over its registration properties
// or during format negotiation -- never reaches LockForProcess, so an absent
// log would otherwise be indistinguishable from never having been created.
DreamApo::DreamApo(IUnknown *outer)
    : m_outer(outer)
{
    trace(L"ctor aggregated", outer ? 1u : 0u);
}

DreamApo::~DreamApo()
{
    // Normally released by UnlockForProcess. Repeated here so an instance torn
    // down without one -- which the engine is entitled to do -- cannot leave the
    // channel holding a reference to freed memory.
    if (m_channelHeld && m_channel) {
        m_channel->release(this);
        m_channelHeld = false;
    }
    trace(L"dtor");
}

// ------------------------------------------------------------------ IUnknown
//
// The delegating half. When the engine has aggregated us, every one of these
// belongs to the outer object; answering them ourselves would hand a client two
// different identities for what is supposed to be one COM object.

ULONG DreamApo::AddRef()
{
    return m_outer ? m_outer->AddRef() : NonDelegatingAddRef();
}

ULONG DreamApo::Release()
{
    return m_outer ? m_outer->Release() : NonDelegatingRelease();
}

HRESULT DreamApo::QueryInterface(REFIID riid, void **ppv)
{
    return m_outer ? m_outer->QueryInterface(riid, ppv)
                   : NonDelegatingQueryInterface(riid, ppv);
}

// The non-delegating half: our own identity and our own ref count. This is what
// the class factory returns to the aggregator, and what the aggregator calls
// once it has decided which of our interfaces it wants.

ULONG DreamApo::NonDelegatingAddRef()
{
    return m_ref.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG DreamApo::NonDelegatingRelease()
{
    const ULONG n = m_ref.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (n == 0)
        delete this;
    return n;
}

HRESULT DreamApo::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;

    // IUnknown must resolve to the *non-delegating* one, or the aggregator
    // could never release the inner object it owns.
    if (riid == __uuidof(IUnknown))
        *ppv = static_cast<INonDelegatingUnknown *>(this);
    else if (riid == __uuidof(IAudioProcessingObject))
        *ppv = static_cast<IAudioProcessingObject *>(this);
    else if (riid == __uuidof(IAudioProcessingObjectConfiguration))
        *ppv = static_cast<IAudioProcessingObjectConfiguration *>(this);
    else if (riid == __uuidof(IAudioProcessingObjectRT))
        *ppv = static_cast<IAudioProcessingObjectRT *>(this);
    else if (riid == __uuidof(IAudioSystemEffects))
        *ppv = static_cast<IAudioSystemEffects *>(this);
    else
        return E_NOINTERFACE;

    // AddRef through whatever was just handed out, not unconditionally through
    // ours: INonDelegatingUnknown lands on NonDelegatingAddRef and counts this
    // object, while every other interface belongs to the outer identity and
    // must count that one instead. INonDelegatingUnknown is laid out exactly
    // like IUnknown for the sake of this cast.
    reinterpret_cast<IUnknown *>(*ppv)->AddRef();
    return S_OK;
}

// ------------------------------------------------- IAudioProcessingObject

HRESULT DreamApo::Reset()
{
    m_chain.reset();
    return S_OK;
}

HRESULT DreamApo::GetLatency(HNSTIME *pTime)
{
    if (!pTime)
        return E_POINTER;

    // Zero until LockForProcess has established a rate: this is queried during
    // format negotiation, before any rate or impulse response exists.
    //
    // Everything except convolution is sample-by-sample with no lookahead. The
    // convolver delays by exactly one of its blocks, and that figure is fixed
    // for the life of the lock -- which is why arming, rather than merely
    // enabling, is what turns it on. Under-reporting causes no glitch, but
    // clients sum this across the whole chain and feed it to video
    // synchronisation, and the capture-path echo canceller uses the render
    // stream as its reference; being wrong there is a synchronisation bug that
    // shows up somewhere else entirely.
    //
    // The per-channel delay is summed in as well. Unlike the convolver's, its
    // figure follows a parameter the user can change at any time, so a delay
    // dialled in after the stream was built leaves this stale until the stream
    // is rebuilt. Time alignment is a few milliseconds, which is below what
    // video synchronisation resolves; there is no way to have both a live
    // control and a fixed reported latency.
    const uint32_t samples = m_locked ? uint32_t(m_chain.latencySamples()) : 0u;
    *pTime = (samples > 0 && m_sampleRate > 0.0)
                 ? HNSTIME(std::llround(double(samples) * 10000000.0 / m_sampleRate))
                 : 0;
    return S_OK;
}

HRESULT DreamApo::GetRegistrationProperties(APO_REG_PROPERTIES **ppRegProps)
{
    if (!ppRegProps)
        return E_POINTER;
    trace(L"GetRegistrationProperties");
    auto *copy = static_cast<APO_REG_PROPERTIES *>(::CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES)));
    if (!copy)
        return E_OUTOFMEMORY;
    *copy = g_regProperties;
    *ppRegProps = copy;
    return S_OK;
}

HRESULT DreamApo::Initialize(UINT32 cbDataSize, BYTE *pbyData)
{
    // The blob is deliberately ignored. Parameters arrive over ParamChannel,
    // which can deliver them again whenever the user moves a slider; anything
    // passed at construction time would be frozen for the life of the stream.
    trace(L"Initialize bytes", cbDataSize);
    (void)pbyData;
    return S_OK;
}

bool DreamApo::formatAcceptable(IAudioMediaType *type, WAVEFORMATEX **out) const
{
    if (!type)
        return false;

    auto *wfx = const_cast<WAVEFORMATEX *>(type->GetAudioFormat());
    if (!wfx)
        return false;

    // Float32 only. The audio engine's mix format is float, and refusing
    // anything else keeps a whole class of conversion bugs out of the
    // real-time path.
    const bool isFloat = (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
                         || (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                             && reinterpret_cast<WAVEFORMATEXTENSIBLE *>(wfx)->SubFormat
                                    == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    if (!isFloat || wfx->wBitsPerSample != 32)
        return false;

    // Bounds, not politeness. m_channels is taken straight from this field and
    // sizes every buffer downstream, and the effects are written against
    // kMaxChannels = 8 -- so a 7.1.4 endpoint would not merely be unsupported,
    // it would silently lose four channels. Refusing the format is the correct
    // failure: Windows then runs the endpoint without us and audio is intact.
    if (wfx->nChannels < 1 || wfx->nChannels > 8)
        return false;
    if (wfx->nSamplesPerSec < 8000 || wfx->nSamplesPerSec > 384000)
        return false;

    if (out)
        *out = wfx;
    return true;
}

HRESULT DreamApo::IsInputFormatSupported(IAudioMediaType *pOppositeFormat,
                                         IAudioMediaType *pRequestedInputFormat,
                                         IAudioMediaType **ppSupportedInputFormat)
{
    (void)pOppositeFormat;
    if (!pRequestedInputFormat)
        return E_POINTER;

    WAVEFORMATEX *wfx = nullptr;
    const bool ok = formatAcceptable(pRequestedInputFormat, &wfx);
    trace(ok ? L"IsFormatSupported ok tag/ch/rate" : L"IsFormatSupported REJECTED tag/ch/rate",
          wfx ? wfx->wFormatTag : 0, wfx ? wfx->nChannels : 0,
          wfx ? wfx->nSamplesPerSec : 0);

    if (ok) {
        if (ppSupportedInputFormat) {
            *ppSupportedInputFormat = pRequestedInputFormat;
            pRequestedInputFormat->AddRef();
        }
        return S_OK;
    }
    if (ppSupportedInputFormat)
        *ppSupportedInputFormat = nullptr;
    return APOERR_FORMAT_NOT_SUPPORTED;
}

HRESULT DreamApo::IsOutputFormatSupported(IAudioMediaType *pOppositeFormat,
                                          IAudioMediaType *pRequestedOutputFormat,
                                          IAudioMediaType **ppSupportedOutputFormat)
{
    return IsInputFormatSupported(pOppositeFormat, pRequestedOutputFormat,
                                  ppSupportedOutputFormat);
}

HRESULT DreamApo::GetInputChannelCount(UINT32 *pu32ChannelCount)
{
    if (!pu32ChannelCount)
        return E_POINTER;
    *pu32ChannelCount = m_channels ? m_channels : 2;
    return S_OK;
}

// ------------------------------ IAudioProcessingObjectConfiguration

HRESULT DreamApo::LockForProcess(UINT32 u32NumInputConnections,
                                 APO_CONNECTION_DESCRIPTOR **ppInputConnections,
                                 UINT32 u32NumOutputConnections,
                                 APO_CONNECTION_DESCRIPTOR **ppOutputConnections)
{
    if (u32NumInputConnections != 1 || u32NumOutputConnections != 1
        || !ppInputConnections || !ppOutputConnections
        || !ppInputConnections[0] || !ppOutputConnections[0]) {
        return E_INVALIDARG;
    }

    WAVEFORMATEX *wfx = nullptr;
    if (!formatAcceptable(ppInputConnections[0]->pFormat, &wfx) || !wfx)
        return APOERR_FORMAT_NOT_SUPPORTED;

    m_channels = wfx->nChannels;
    m_sampleRate = wfx->nSamplesPerSec;
    m_maxFrames = ppInputConnections[0]->u32MaxFrameCount;

    // formatAcceptable has already bounded the channel count and rate. The
    // frame count comes from the connection rather than the format, so it is
    // checked here -- it sizes every buffer below.
    if (m_maxFrames < 1 || m_maxFrames > 65536)
        return E_INVALIDARG;

    // Every allocation for the streaming path happens here, on a normal
    // thread, never inside APOProcess.
    m_scratch.assign(size_t(m_channels) * size_t(m_maxFrames), 0.0f);
    m_channelPtrs.assign(size_t(m_channels), nullptr);
    for (UINT32 c = 0; c < m_channels; ++c)
        m_channelPtrs[c] = m_scratch.data() + size_t(c) * size_t(m_maxFrames);

    m_chain.prepare(m_sampleRate, int(m_channels), int(m_maxFrames));

    // Zeroed, not value-initialised: enableMask == 0 is what makes a stream
    // that never receives parameters bit-transparent.
    std::memset(&m_pending, 0, sizeof m_pending);

    m_framesProcessed.store(0, std::memory_order_relaxed);
    m_peakIn = 0.0f;
    m_peakOut = 0.0f;
    m_statusFlags.store(dsp::kSfStreaming | dsp::kSfPassthrough, std::memory_order_relaxed);

    Reset();

    m_channel = &ParamChannel::instance();
    m_channelHeld = m_channel->acquire(this);

    // Arming is latched here and never changes for the life of the lock,
    // because the alignment delay it introduces is what GetLatency reports and
    // the engine asks for that once. An impulse response chosen for the first
    // time therefore engages on the next stream rather than mid-playback --
    // deferred rather than lied about.
    if (m_channelHeld) {
        dsp::ParamBlock initial;
        std::memset(&initial, 0, sizeof initial);
        m_channel->slots().read(&initial);
        m_chain.convolution().setArmed(initial.convolution.irGeneration != 0u);
        if (m_chain.convolution().armed()) {
            trace(L"convolution armed, latency samples",
                  m_chain.latencySamples());
        }
    }

    m_locked = true;
    trace(L"LockForProcess ch/rate/maxFrames",
          m_channels, unsigned(m_sampleRate), m_maxFrames);
    if (!m_channelHeld)
        trace(L"param channel unavailable; running transparent");
    return S_OK;
}

void DreamApo::fillStatus(dsp::StatusInstance &out) const noexcept
{
    out.sampleRate = uint32_t(m_sampleRate);
    out.channels = m_channels;
    out.maxFrames = m_maxFrames;
    out.appliedGeneration = m_chain.appliedGeneration();
    out.appliedMask = m_chain.appliedMask();
    out.flags = m_statusFlags.load(std::memory_order_relaxed);
    out.framesProcessed = m_framesProcessed.load(std::memory_order_relaxed);
}

HRESULT DreamApo::UnlockForProcess()
{
    // Peaks in thousandths, because trace() only carries integers. A non-zero
    // frame count proves the DSP was in the signal path rather than merely
    // instantiated; peak-out differing from peak-in proves it changed the audio.
    trace(L"UnlockForProcess frames/peakIn/peakOut(1000x)",
          unsigned(m_framesProcessed.load(std::memory_order_relaxed)),
          unsigned(m_peakIn * 1000.0f),
          unsigned(m_peakOut * 1000.0f));

    // Released before the buffers are torn down, so the status writer can never
    // find this instance in a half-dismantled state.
    if (m_channelHeld && m_channel) {
        m_channel->release(this);
        m_channelHeld = false;
    }

    m_locked = false;
    m_statusFlags.store(0, std::memory_order_relaxed);
    m_framesProcessed.store(0, std::memory_order_relaxed);
    m_peakIn = 0.0f;
    m_peakOut = 0.0f;
    m_scratch.clear();
    m_channelPtrs.clear();
    m_channels = 0;
    m_maxFrames = 0;
    return S_OK;
}

// ----------------------------------------- IAudioProcessingObjectRT

UINT32 DreamApo::CalcInputFrames(UINT32 u32OutputFrameCount)  { return u32OutputFrameCount; }
UINT32 DreamApo::CalcOutputFrames(UINT32 u32InputFrameCount)  { return u32InputFrameCount; }

void DreamApo::APOProcess(UINT32 u32NumInputConnections,
                          APO_CONNECTION_PROPERTY **ppInputConnections,
                          UINT32 u32NumOutputConnections,
                          APO_CONNECTION_PROPERTY **ppOutputConnections)
{
    if (u32NumInputConnections < 1 || u32NumOutputConnections < 1
        || !ppInputConnections || !ppOutputConnections) {
        return;
    }

    APO_CONNECTION_PROPERTY *in = ppInputConnections[0];
    APO_CONNECTION_PROPERTY *out = ppOutputConnections[0];
    if (!in || !out)
        return;

    const auto *src = reinterpret_cast<const float *>(in->pBuffer);
    auto *dst = reinterpret_cast<float *>(out->pBuffer);
    const UINT32 frames = in->u32ValidFrameCount;

    out->u32ValidFrameCount = frames;
    out->u32BufferFlags = in->u32BufferFlags;

    // Silence and invalid buffers pass straight through: the engine is allowed
    // to hand us a buffer marked silent with undefined contents.
    if (!m_locked || !src || !dst || frames == 0
        || frames > m_maxFrames || m_channels == 0
        || in->u32BufferFlags != BUFFER_VALID) {
        // dst may *be* src: APO_FLAG_INPLACE is set, and memcpy over identical
        // pointers is undefined. When they are the same the data is already
        // where it needs to be.
        if (src && dst && dst != src && frames > 0 && m_channels > 0)
            ::memcpy(dst, src, size_t(frames) * size_t(m_channels) * sizeof(float));
        return;
    }

    const UINT32 ch = m_channels;

    // --- ingest --------------------------------------------------------
    // Two atomic loads and at most two 256-byte copies. read() makes exactly
    // two attempts and never spins; if both catch the publisher mid-write,
    // m_pending keeps what it already had -- always a sanitised block, zeroed
    // by LockForProcess.
    if (m_channelHeld && m_channel)
        (void)m_channel->slots().read(&m_pending);

    // --- install -------------------------------------------------------
    // Eight memcmps on an unchanged block. Filter design only runs for effects
    // that are enabled and whose parameters actually differ, so this costs
    // nothing on the overwhelming majority of callbacks.
    m_chain.apply(m_pending);

    uint32_t flags = dsp::kSfStreaming;
    if (dst == src)
        flags |= dsp::kSfInPlace;

    // --- transparent fast path -----------------------------------------
    // Not one float is loaded, no arithmetic happens, the floating-point mode
    // is not touched, and dst comes out byte-identical to src. This is also the
    // master bypass: the GUI writes enableMask = 0 and gets exactly this.
    if (m_chain.appliedMask() == 0u) {
        if (dst != src)
            ::memcpy(dst, src, size_t(frames) * size_t(ch) * sizeof(float));
        m_framesProcessed.fetch_add(frames, std::memory_order_relaxed);
        m_statusFlags.store(flags | dsp::kSfPassthrough, std::memory_order_relaxed);
        return;
    }
    m_statusFlags.store(flags, std::memory_order_relaxed);

    // --- denormal control ----------------------------------------------
    // Every recursive filter here decays into denormal range once the input
    // goes quiet, where each operation costs 50-100x more -- a CPU spike
    // arriving exactly when there is no signal left to hide a dropout behind.
    // RAII, because this thread is shared with other vendors' processing
    // objects and their arithmetic must not be left altered.
    const dsp::DenormalGuard denormals;

    // Deinterleave -> process -> reinterleave. The DSP layer works on planar
    // buffers, which is also what makes it testable offline.
    for (UINT32 i = 0; i < frames; ++i) {
        for (UINT32 c = 0; c < ch; ++c) {
            const float v = src[size_t(i) * ch + c];
            m_channelPtrs[c][i] = v;
            const float a = v < 0.0f ? -v : v;
            if (a > m_peakIn)
                m_peakIn = a;
        }
    }

    dsp::AudioBuffer buf{ m_channelPtrs.data(), int(ch), int(frames) };
    m_chain.process(buf);

    for (UINT32 i = 0; i < frames; ++i) {
        for (UINT32 c = 0; c < ch; ++c) {
            const float v = m_channelPtrs[c][i];
            dst[size_t(i) * ch + c] = v;
            const float a = v < 0.0f ? -v : v;
            if (a > m_peakOut)
                m_peakOut = a;
        }
    }
    m_framesProcessed.fetch_add(frames, std::memory_order_relaxed);
}

} // namespace dreamdsp::apo
