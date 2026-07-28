#include "DreamApo.h"

#include <new>

namespace dreamdsp::apo {

// {6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E10}
const CLSID CLSID_DreamDspApo =
    { 0x6d2f1c55, 0x5e4b, 0x4a7e, { 0x9c, 0x31, 0x0d, 0x5a, 0x6c, 0x4b, 0x7e, 0x10 } };

namespace {

// Registration properties are handed to the audio engine as a plain struct.
// Static so nothing is allocated when the engine asks for them.
APO_REG_PROPERTIES g_regProperties = {
    /* clsid              */ { 0x6d2f1c55, 0x5e4b, 0x4a7e, { 0x9c, 0x31, 0x0d, 0x5a, 0x6c, 0x4b, 0x7e, 0x10 } },
    /* Flags              */ APO_FLAG_DEFAULT,
    /* szFriendlyName     */ L"DreamDSP Effects",
    /* szCopyrightInfo    */ L"DreamDSP",
    /* u32MajorVersion    */ 0,
    /* u32MinorVersion    */ 1,
    /* u32MinInputConnections   */ 1,
    /* u32MaxInputConnections   */ 1,
    /* u32MinOutputConnections  */ 1,
    /* u32MaxOutputConnections  */ 1,
    /* u32MaxInstances    */ 0,
    /* u32NumAPOInterfaces*/ 1,
    /* iidAPOInterfaceList*/ { __uuidof(IAudioSystemEffects) },
};

} // namespace

DreamApo::DreamApo() = default;
DreamApo::~DreamApo() = default;

// ------------------------------------------------------------------ IUnknown

ULONG DreamApo::AddRef()
{
    return m_ref.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG DreamApo::Release()
{
    const ULONG n = m_ref.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (n == 0)
        delete this;
    return n;
}

HRESULT DreamApo::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;

    if (riid == __uuidof(IUnknown))
        *ppv = static_cast<IAudioProcessingObject *>(this);
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

    AddRef();
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
    (void)cbDataSize;
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

    if (formatAcceptable(pRequestedInputFormat, nullptr)) {
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
    return S_OK;
}

HRESULT DreamApo::UnlockForProcess()
{
    m_locked = false;
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
    for (UINT32 i = 0; i < frames; ++i)
        for (UINT32 c = 0; c < ch; ++c)
            m_channelPtrs[c][i] = src[size_t(i) * ch + c];

    dsp::AudioBuffer buf{ m_channelPtrs.data(), int(ch), int(frames) };
    m_comp.process(buf);
    m_reverb.process(buf);

    for (UINT32 i = 0; i < frames; ++i)
        for (UINT32 c = 0; c < ch; ++c)
            dst[size_t(i) * ch + c] = m_channelPtrs[c][i];
}

} // namespace dreamdsp::apo
