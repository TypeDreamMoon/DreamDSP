#pragma once

#include <windows.h>
#include <audioenginebaseapo.h>
#include <audioclient.h>

#include <atomic>
#include <vector>

#include "Compressor.h"
#include "MultibandCompressor.h"
#include "Reverb.h"
#include "Saturation.h"
#include "Stereo.h"

namespace dreamdsp::apo {

// {6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E10}
extern const CLSID CLSID_DreamDspApo;

// The audio processing object itself.
//
// This runs inside audiodg.exe, which is the reason for every unusual rule in
// here: APOProcess must not allocate, must not lock, must not block and must
// not throw. Anything that needs memory is sized in LockForProcess, which is
// called on a normal thread before streaming starts. A mistake in this file
// does not crash an application -- it takes out every sound on the machine.
//
// The DSP itself lives in dreamdsp_core and has already been verified offline;
// nothing here is allowed to be the first place an algorithm runs.
class DreamApo final : public IAudioProcessingObjectRT,
                       public IAudioProcessingObjectConfiguration,
                       public IAudioProcessingObject,
                       public IAudioSystemEffects
{
public:
    DreamApo();
    virtual ~DreamApo();

    // --- IUnknown -------------------------------------------------------
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override;

    // --- IAudioProcessingObject -----------------------------------------
    STDMETHOD(Reset)() override;
    STDMETHOD(GetLatency)(HNSTIME *pTime) override;
    STDMETHOD(GetRegistrationProperties)(APO_REG_PROPERTIES **ppRegProps) override;
    STDMETHOD(Initialize)(UINT32 cbDataSize, BYTE *pbyData) override;
    STDMETHOD(IsInputFormatSupported)(IAudioMediaType *pOppositeFormat,
                                      IAudioMediaType *pRequestedInputFormat,
                                      IAudioMediaType **ppSupportedInputFormat) override;
    STDMETHOD(IsOutputFormatSupported)(IAudioMediaType *pOppositeFormat,
                                       IAudioMediaType *pRequestedOutputFormat,
                                       IAudioMediaType **ppSupportedOutputFormat) override;
    STDMETHOD(GetInputChannelCount)(UINT32 *pu32ChannelCount) override;

    // --- IAudioProcessingObjectConfiguration -----------------------------
    STDMETHOD(LockForProcess)(UINT32 u32NumInputConnections,
                              APO_CONNECTION_DESCRIPTOR **ppInputConnections,
                              UINT32 u32NumOutputConnections,
                              APO_CONNECTION_DESCRIPTOR **ppOutputConnections) override;
    STDMETHOD(UnlockForProcess)() override;

    // --- IAudioProcessingObjectRT ----------------------------------------
    STDMETHOD_(void, APOProcess)(UINT32 u32NumInputConnections,
                                 APO_CONNECTION_PROPERTY **ppInputConnections,
                                 UINT32 u32NumOutputConnections,
                                 APO_CONNECTION_PROPERTY **ppOutputConnections) override;
    STDMETHOD_(UINT32, CalcInputFrames)(UINT32 u32OutputFrameCount) override;
    STDMETHOD_(UINT32, CalcOutputFrames)(UINT32 u32InputFrameCount) override;

private:
    bool formatAcceptable(IAudioMediaType *type, WAVEFORMATEX **out) const;

    std::atomic<ULONG> m_ref{ 1 };
    bool m_locked = false;

    UINT32 m_channels = 0;
    UINT32 m_maxFrames = 0;
    double m_sampleRate = 0.0;

    // Deinterleaved scratch, sized once in LockForProcess. APOProcess only
    // ever writes into what is already here.
    std::vector<float> m_scratch;
    std::vector<float *> m_channelPtrs;

    dsp::Compressor m_comp;
    dsp::Reverb m_reverb;
};

} // namespace dreamdsp::apo
