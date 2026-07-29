#include "DreamApo.h"

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
    // APOProcess copies through its own deinterleaved scratch, so that is safe,
    // and it saves the engine a buffer. Channel count is deliberately not
    // constrained: the DSP works for any count, so SAMPLESPERFRAME_MUST_MATCH
    // (part of APO_FLAG_DEFAULT) would only refuse formats we can handle.
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

DreamApo::~DreamApo() { trace(L"dtor"); }

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
    m_comp.reset();
    m_reverb.reset();
    return S_OK;
}

HRESULT DreamApo::GetLatency(HNSTIME *pTime)
{
    if (!pTime)
        return E_POINTER;
    // Everything here is sample-by-sample; no lookahead, no block delay.
    *pTime = 0;
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
    // No initialisation blob yet. Parameters will arrive over shared memory
    // once there is a control channel; a blob passed at construction time
    // could not be changed while streaming anyway.
    trace(L"Initialize bytes", cbDataSize);
    (void)pbyData;
    return S_OK;
}

bool DreamApo::formatAcceptable(IAudioMediaType *type, WAVEFORMATEX **out) const
{
    if (!type)
        return false;
    const UNCOMPRESSEDAUDIOFORMAT *uncompressed = nullptr;
    if (FAILED(type->GetUncompressedAudioFormat(const_cast<UNCOMPRESSEDAUDIOFORMAT *>(uncompressed))))
        { /* fall through to the WAVEFORMATEX path */ }

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

    // Every allocation for the streaming path happens here, on a normal
    // thread, never inside APOProcess.
    m_scratch.assign(size_t(m_channels) * size_t(m_maxFrames), 0.0f);
    m_channelPtrs.assign(size_t(m_channels), nullptr);
    for (UINT32 c = 0; c < m_channels; ++c)
        m_channelPtrs[c] = m_scratch.data() + size_t(c) * size_t(m_maxFrames);

    m_comp.prepare(m_sampleRate, int(m_channels));
    m_reverb.prepare(m_sampleRate);
    Reset();

    m_locked = true;
    trace(L"LockForProcess ch/rate/maxFrames",
          m_channels, unsigned(m_sampleRate), m_maxFrames);
    return S_OK;
}

HRESULT DreamApo::UnlockForProcess()
{
    // Peaks in thousandths, because trace() only carries integers. A non-zero
    // frame count proves the DSP was in the signal path rather than merely
    // instantiated; peak-out differing from peak-in proves it changed the audio.
    trace(L"UnlockForProcess frames/peakIn/peakOut(1000x)",
          unsigned(m_framesProcessed),
          unsigned(m_peakIn * 1000.0f),
          unsigned(m_peakOut * 1000.0f));
    m_locked = false;
    m_framesProcessed = 0;
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
        if (src && dst && frames > 0 && m_channels > 0)
            ::memcpy(dst, src, size_t(frames) * size_t(m_channels) * sizeof(float));
        return;
    }

    // Deinterleave -> process -> reinterleave. The DSP layer works on planar
    // buffers, which is also what makes it testable offline.
    const UINT32 ch = m_channels;
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
    m_comp.process(buf);
    m_reverb.process(buf);

    for (UINT32 i = 0; i < frames; ++i) {
        for (UINT32 c = 0; c < ch; ++c) {
            const float v = m_channelPtrs[c][i];
            dst[size_t(i) * ch + c] = v;
            const float a = v < 0.0f ? -v : v;
            if (a > m_peakOut)
                m_peakOut = a;
        }
    }
    m_framesProcessed += frames;
}

} // namespace dreamdsp::apo
