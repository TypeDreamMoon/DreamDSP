#pragma once

#include <windows.h>
#include <audioenginebaseapo.h>
#include <audioclient.h>

#include <atomic>
#include <vector>

#include "EffectChain.h"
#include "ParamBlock.h"
#include "StatusBlock.h"

namespace dreamdsp::apo {

class ParamChannel;

// {6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E10}
extern const CLSID CLSID_DreamDspApo;

// Appends a line to C:\ProgramData\DreamDSP\apo.log. There is no debugger and
// no console inside audiodg.exe, and a rejected APO produces no event log entry
// either, so this is the only way to see what happened. Opens a file -- safe
// from the COM entry points, never callable from APOProcess.
void trace(const wchar_t *what, unsigned a = 0, unsigned b = 0, unsigned c = 0);

// The audio engine creates APOs *aggregated*: it passes its own outer IUnknown
// to IClassFactory::CreateInstance and expects the inner object's
// non-delegating IUnknown back. A class factory that answers
// CLASS_E_NOAGGREGATION is skipped without a word -- no event log entry, and
// the DLL is dropped again before anything in it runs. That is why this
// separate, non-delegating interface exists alongside the normal IUnknown that
// the three APO interfaces inherit.
class INonDelegatingUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void **ppv) = 0;
    virtual ULONG STDMETHODCALLTYPE NonDelegatingAddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE NonDelegatingRelease() = 0;
};

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
                       public IAudioSystemEffects,
                       public INonDelegatingUnknown
{
public:
    // outer is the engine's controlling IUnknown, or null when created
    // stand-alone. It is deliberately not AddRef'd: the outer object owns this
    // one, and a reference back would be a cycle neither side could break.
    explicit DreamApo(IUnknown *outer);
    virtual ~DreamApo();

    // --- IUnknown (delegating) ------------------------------------------
    // Once aggregated these must forward to the outer object, so that a client
    // holding any interface of the pair sees one identity and one ref count.
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override;

    // --- INonDelegatingUnknown ------------------------------------------
    STDMETHOD(NonDelegatingQueryInterface)(REFIID riid, void **ppv) override;
    STDMETHOD_(ULONG, NonDelegatingAddRef)() override;
    STDMETHOD_(ULONG, NonDelegatingRelease)() override;

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

    // Fills in this instance's row of the status block. Called from the
    // watcher thread, so it only reads atomics and values that are fixed
    // between LockForProcess and UnlockForProcess.
    void fillStatus(dsp::StatusInstance &out) const noexcept;

    // What the impulse-response builder needs to know. All fixed between
    // LockForProcess and UnlockForProcess, so the builder thread can read them
    // without synchronisation for as long as it holds a channel reference.
    double streamRate() const noexcept { return m_sampleRate; }
    int streamChannels() const noexcept { return int(m_channels); }
    uint32_t channelMask() const noexcept { return m_channelMask; }
    dsp::EffectChain &chain() noexcept { return m_chain; }

private:
    bool formatAcceptable(IAudioMediaType *type, WAVEFORMATEX **out) const;

    IUnknown *m_outer = nullptr;   // borrowed; see the constructor comment
    std::atomic<ULONG> m_ref{ 1 };
    bool m_locked = false;

    UINT32 m_channels = 0;
    // From WAVEFORMATEXTENSIBLE, so the builder can find the LFE channel and
    // leave it alone. Zero when the format did not carry one.
    uint32_t m_channelMask = 0;
    UINT32 m_maxFrames = 0;
    double m_sampleRate = 0.0;

    // Deinterleaved scratch, sized once in LockForProcess. APOProcess only
    // ever writes into what is already here.
    std::vector<float> m_scratch;
    std::vector<float *> m_channelPtrs;

    // Evidence that the DSP actually ran, reported once from UnlockForProcess.
    // Being instantiated and being in the signal path are different things, and
    // from outside audiodg they look identical.
    float m_peakIn = 0.0f;
    float m_peakOut = 0.0f;

    // Read by the status writer on the watcher thread while the callback is
    // updating them, hence atomic. Relaxed throughout: these are a report, not
    // a synchronisation mechanism, and a status file one block out of date is
    // of no consequence.
    std::atomic<unsigned long long> m_framesProcessed{ 0 };
    std::atomic<uint32_t> m_statusFlags{ 0 };

    // The last block read from the channel, kept so a callback that catches the
    // publisher mid-write can carry on with what it already had. A member, not
    // a local, so no allocation and no per-callback initialisation.
    dsp::ParamBlock m_pending{};
    ParamChannel *m_channel = nullptr;
    bool m_channelHeld = false;

    dsp::EffectChain m_chain;
};

} // namespace dreamdsp::apo
