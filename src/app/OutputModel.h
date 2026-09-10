#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "ParamBlock.h"

#include "app/Defaults.h"

namespace dreamdsp {

// The output stage: channel routing, per-channel delay, loudness correction.
//
// Kept apart from EffectsModel because these three are not effects. They
// describe the physical output -- which speaker a stream channel ends up in,
// how far away it is, and how loud it is actually being played -- and they run
// at the two ends of the chain rather than in the middle of it.
class OutputModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // --- channel matrix ---------------------------------------------------
    Q_PROPERTY(bool matrixOn READ matrixOn WRITE setMatrixOn NOTIFY changed)
    // Which named layout the matrix currently is, or -1 for one edited by hand.
    Q_PROPERTY(int matrixPreset READ matrixPreset NOTIFY changed)
    Q_PROPERTY(QVariantList matrixPresets READ matrixPresets CONSTANT)

    // --- per-channel delay ------------------------------------------------
    Q_PROPERTY(bool delayOn READ delayOn WRITE setDelayOn NOTIFY changed)
    // The largest delay in use, which is what the stream is held back by.
    Q_PROPERTY(double maxDelayMs READ maxDelayMs NOTIFY changed)

    // --- loudness correction ----------------------------------------------
    Q_PROPERTY(bool loudnessOn READ loudnessOn WRITE setLoudnessOn NOTIFY changed)
    Q_PROPERTY(double loudnessAmount READ loudnessAmount WRITE setLoudnessAmount NOTIFY changed)
    Q_PROPERTY(double referenceDb READ referenceDb WRITE setReferenceDb NOTIFY changed)
    // The endpoint's current volume, read from Windows. Not editable here --
    // this is a reading, not a setting.
    Q_PROPERTY(double volumeDb READ volumeDb NOTIFY volumeChanged)
    Q_PROPERTY(bool volumeKnown READ volumeKnown NOTIFY volumeChanged)

    // What the correction currently amounts to. Computed by the DSP layer's own
    // function, so the numbers on screen are the ones being applied.
    Q_PROPERTY(double lowShelfDb READ lowShelfDb NOTIFY shelvesChanged)
    Q_PROPERTY(double highShelfDb READ highShelfDb NOTIFY shelvesChanged)
    Q_PROPERTY(double loudnessPreampDb READ loudnessPreampDb NOTIFY shelvesChanged)

    // --- limiter ----------------------------------------------------------
    Q_PROPERTY(bool limiterOn READ limiterOn WRITE setLimiterOn NOTIFY changed)
    Q_PROPERTY(double limiterGain READ limiterGain WRITE setLimiterGain NOTIFY changed)
    Q_PROPERTY(double limiterThreshold READ limiterThreshold WRITE setLimiterThreshold NOTIFY changed)
    Q_PROPERTY(double limiterRelease READ limiterRelease WRITE setLimiterRelease NOTIFY changed)
    Q_PROPERTY(double limiterLookahead READ limiterLookahead WRITE setLimiterLookahead NOTIFY changed)
    Q_PROPERTY(bool limiterTruePeak READ limiterTruePeak WRITE setLimiterTruePeak NOTIFY changed)

    // How many channels the endpoint actually has, so the interface does not
    // offer eight faders to a pair of headphones. Taken from the APO's own
    // report where there is one.
    Q_PROPERTY(int channels READ channels WRITE setChannels NOTIFY channelsChanged)
    Q_PROPERTY(QStringList channelNames READ channelNames NOTIFY channelsChanged)

public:
    explicit OutputModel(QObject *parent = nullptr);

    // The value this control would have had out of the box, for the reset
    // button beside it. Empty for a name that is not a property of this model.
    Q_INVOKABLE QVariant defaultOf(const QString &name) const
    {
        return defaultPropertyOf<OutputModel>(name);
    }


    static constexpr int kMaxChannels = dsp::ChannelMatrix::kMaxChannels;

    bool matrixOn() const { return m_matrixOn; }
    void setMatrixOn(bool on);
    int matrixPreset() const;
    QVariantList matrixPresets() const;
    Q_INVOKABLE void applyMatrixPreset(int index);
    Q_INVOKABLE double matrixGain(int out, int in) const;
    Q_INVOKABLE void setMatrixGain(int out, int in, double gain);

    bool delayOn() const { return m_delayOn; }
    void setDelayOn(bool on);
    Q_INVOKABLE double delayMs(int channel) const;
    Q_INVOKABLE void setDelayMs(int channel, double ms);
    // Centimetres at 343 m/s, which is how speaker alignment is actually
    // measured. One display, two units, no second source of truth.
    Q_INVOKABLE double delayCm(int channel) const;
    Q_INVOKABLE void setDelayCm(int channel, double cm);
    double maxDelayMs() const;

    bool loudnessOn() const { return m_loudnessOn; }
    void setLoudnessOn(bool on);
    double loudnessAmount() const { return m_loudness.amount; }
    void setLoudnessAmount(double v);
    double referenceDb() const { return m_loudness.referenceDb; }
    void setReferenceDb(double v);
    // Pins the reference to whatever the volume is right now, which is the
    // gesture the parameter actually means: "this is my normal listening level".
    Q_INVOKABLE void useCurrentVolumeAsReference();

    double volumeDb() const { return m_volumeDb; }
    bool volumeKnown() const { return m_volumeKnown; }
    // Called by the controller with a fresh reading.
    void setVolume(double db, bool known);

    double lowShelfDb() const { return m_lowDb; }
    double highShelfDb() const { return m_highDb; }
    double loudnessPreampDb() const { return m_preampDb; }

    bool limiterOn() const { return m_limiterOn; }
    void setLimiterOn(bool on);
    double limiterGain() const { return m_limiter.gainDb; }
    void setLimiterGain(double v);
    double limiterThreshold() const { return m_limiter.thresholdDb; }
    void setLimiterThreshold(double v);
    double limiterRelease() const { return m_limiter.releaseMs; }
    void setLimiterRelease(double v);
    double limiterLookahead() const { return m_limiter.lookaheadMs; }
    void setLimiterLookahead(double v);
    bool limiterTruePeak() const { return m_limiter.truePeak; }
    void setLimiterTruePeak(bool v);
    dsp::Limiter::Params limiterParams() const { return m_limiter; }

    int channels() const { return m_channels; }
    void setChannels(int n);
    QStringList channelNames() const;

    // --- wire format ------------------------------------------------------
    dsp::ChannelMatrix::Params matrixParams() const { return m_matrix; }
    dsp::ChannelDelay::Params delayParams() const { return m_delay; }
    dsp::LoudnessCorrection::Params loudnessParams() const;
    uint32_t enableBits() const;

    // Reads everything back out of a published block, so a restart resumes what
    // was running rather than a second copy of it kept somewhere else.
    void restore(const dsp::ParamBlock &b);

signals:
    void changed();
    void volumeChanged();
    void shelvesChanged();
    void channelsChanged();

private:
    void refreshShelves();

    dsp::ChannelMatrix::Params m_matrix{};
    dsp::ChannelDelay::Params m_delay{};
    dsp::LoudnessCorrection::Params m_loudness{};
    dsp::Limiter::Params m_limiter{};

    bool m_matrixOn = false;
    bool m_delayOn = false;
    bool m_loudnessOn = false;
    bool m_limiterOn = false;

    double m_volumeDb = 0.0;
    bool m_volumeKnown = false;
    double m_lowDb = 0.0, m_highDb = 0.0, m_preampDb = 0.0;

    int m_channels = 2;
};

} // namespace dreamdsp
