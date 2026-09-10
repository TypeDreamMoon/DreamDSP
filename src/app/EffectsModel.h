#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include "AutoGain.h"
#include "BassBoost.h"
#include "Clipper.h"
#include "MultibandCompressor.h"
#include "ParamBlock.h"
#include "Saturation.h"
#include "Stereo.h"
#include "Transient.h"

#include "app/Defaults.h"

namespace dreamdsp {

// Parameters for the effects that do not warrant a class each.
//
// One object rather than six: these are all plain parameter bags, and six
// nearly identical QObject wrappers would be more code than the DSP they
// front. The compressor and reverb keep their own models because they have
// behaviour beyond storage -- a transfer curve to plot.
class EffectsModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Owned by AppController, not by the QML engine: it is the only object
    // with a process-long lifetime and with save/restore, and the parameter
    // publisher has to see every change wherever it comes from.
    QML_UNCREATABLE("owned by AppController")

    // --- tube saturation ---
    Q_PROPERTY(bool tubeEnabled MEMBER m_tubeEnabled NOTIFY changed)
    Q_PROPERTY(double tubeDrive READ tubeDrive WRITE setTubeDrive NOTIFY changed)
    Q_PROPERTY(double tubeBias READ tubeBias WRITE setTubeBias NOTIFY changed)
    Q_PROPERTY(double tubeMix READ tubeMix WRITE setTubeMix NOTIFY changed)

    // --- psychoacoustic bass ---
    Q_PROPERTY(bool bassEnabled MEMBER m_bassEnabled NOTIFY changed)
    Q_PROPERTY(double bassCutoff READ bassCutoff WRITE setBassCutoff NOTIFY changed)
    Q_PROPERTY(double bassAmount READ bassAmount WRITE setBassAmount NOTIFY changed)
    Q_PROPERTY(double bassDrive READ bassDrive WRITE setBassDrive NOTIFY changed)
    Q_PROPERTY(bool bassRemoveOriginal READ bassRemoveOriginal WRITE setBassRemoveOriginal NOTIFY changed)

    // --- dynamic bass boost ---
    //
    // Beside the psychoacoustic one deliberately: they look interchangeable and
    // are not. That one invents a fundamental the transducer cannot play; this
    // one lifts a fundamental that is already there, by whatever headroom is
    // left. Which you want depends on the speaker, not on taste.
    Q_PROPERTY(bool dynBassEnabled MEMBER m_dynBassEnabled NOTIFY changed)
    Q_PROPERTY(double dynBassMaxGain READ dynBassMaxGain WRITE setDynBassMaxGain NOTIFY changed)
    Q_PROPERTY(double dynBassCutoff READ dynBassCutoff WRITE setDynBassCutoff NOTIFY changed)
    Q_PROPERTY(double dynBassRelease READ dynBassRelease WRITE setDynBassRelease NOTIFY changed)

    // --- exciter ---
    Q_PROPERTY(bool exciterEnabled MEMBER m_exciterEnabled NOTIFY changed)
    Q_PROPERTY(double exciterFreq READ exciterFreq WRITE setExciterFreq NOTIFY changed)
    Q_PROPERTY(double exciterDrive READ exciterDrive WRITE setExciterDrive NOTIFY changed)
    Q_PROPERTY(double exciterAmount READ exciterAmount WRITE setExciterAmount NOTIFY changed)
    Q_PROPERTY(double exciterThreshold READ exciterThreshold WRITE setExciterThreshold NOTIFY changed)

    // --- stereo width ---
    Q_PROPERTY(bool widthEnabled MEMBER m_widthEnabled NOTIFY changed)
    Q_PROPERTY(double stereoWidth READ stereoWidth WRITE setStereoWidth NOTIFY changed)
    Q_PROPERTY(double monoBelow READ monoBelow WRITE setMonoBelow NOTIFY changed)
    Q_PROPERTY(double widthPhase READ widthPhase WRITE setWidthPhase NOTIFY changed)

    // --- crossfeed ---
    Q_PROPERTY(bool crossfeedEnabled MEMBER m_crossfeedEnabled NOTIFY changed)
    Q_PROPERTY(double crossfeedCutoff READ crossfeedCutoff WRITE setCrossfeedCutoff NOTIFY changed)
    Q_PROPERTY(double crossfeedLevel READ crossfeedLevel WRITE setCrossfeedLevel NOTIFY changed)

    // --- multiband ---
    Q_PROPERTY(bool multibandEnabled MEMBER m_multibandEnabled NOTIFY changed)
    Q_PROPERTY(double lowCross READ lowCross WRITE setLowCross NOTIFY changed)
    Q_PROPERTY(double highCross READ highCross WRITE setHighCross NOTIFY changed)

    // --- transient shaper ---
    Q_PROPERTY(bool transientEnabled MEMBER m_transientEnabled NOTIFY changed)
    Q_PROPERTY(double transientAttack READ transientAttack WRITE setTransientAttack NOTIFY changed)
    Q_PROPERTY(double transientSustain READ transientSustain WRITE setTransientSustain NOTIFY changed)
    Q_PROPERTY(double transientAttackMs READ transientAttackMs WRITE setTransientAttackMs NOTIFY changed)
    Q_PROPERTY(double transientReleaseMs READ transientReleaseMs WRITE setTransientReleaseMs NOTIFY changed)

    // --- soft clipper ---
    Q_PROPERTY(bool clipperEnabled MEMBER m_clipperEnabled NOTIFY changed)
    Q_PROPERTY(double clipperDrive READ clipperDrive WRITE setClipperDrive NOTIFY changed)
    Q_PROPERTY(double clipperCeiling READ clipperCeiling WRITE setClipperCeiling NOTIFY changed)
    Q_PROPERTY(double clipperKnee READ clipperKnee WRITE setClipperKnee NOTIFY changed)
    Q_PROPERTY(bool clipperOversample READ clipperOversample WRITE setClipperOversample NOTIFY changed)

    // --- auto volume ---
    Q_PROPERTY(bool autoGainEnabled MEMBER m_autoGainEnabled NOTIFY changed)
    Q_PROPERTY(double autoGainTarget READ autoGainTarget WRITE setAutoGainTarget NOTIFY changed)
    Q_PROPERTY(double autoGainMax READ autoGainMax WRITE setAutoGainMax NOTIFY changed)
    Q_PROPERTY(double autoGainRate READ autoGainRate WRITE setAutoGainRate NOTIFY changed)
    Q_PROPERTY(double autoGainWindow READ autoGainWindow WRITE setAutoGainWindow NOTIFY changed)

    // --- night mode ---
    Q_PROPERTY(bool nightModeEnabled MEMBER m_nightModeEnabled NOTIFY changed)
    Q_PROPERTY(int nightProfile READ nightProfile WRITE setNightProfile NOTIFY changed)
    Q_PROPERTY(double nightBoost READ nightBoost WRITE setNightBoost NOTIFY changed)
    Q_PROPERTY(double nightCut READ nightCut WRITE setNightCut NOTIFY changed)
    Q_PROPERTY(double nightReference READ nightReference WRITE setNightReference NOTIFY changed)

public:
    explicit EffectsModel(QObject *parent = nullptr);

    // The value this control would have had out of the box, for the reset
    // button beside it. Empty for a name that is not a property of this model.
    Q_INVOKABLE QVariant defaultOf(const QString &name) const
    {
        return defaultPropertyOf<EffectsModel>(name);
    }


    double tubeDrive() const { return m_tube.drive; }   void setTubeDrive(double v);
    double tubeBias() const  { return m_tube.bias; }    void setTubeBias(double v);
    double tubeMix() const   { return m_tube.mix; }     void setTubeMix(double v);

    double bassCutoff() const { return m_bass.cutoffHz; } void setBassCutoff(double v);
    double bassAmount() const { return m_bass.amount; }   void setBassAmount(double v);
    double bassDrive() const  { return m_bass.drive; }    void setBassDrive(double v);
    bool bassRemoveOriginal() const { return m_bass.removeOriginal; }
    void setBassRemoveOriginal(bool v);

    double exciterFreq() const   { return m_exciter.frequencyHz; } void setExciterFreq(double v);
    double exciterDrive() const  { return m_exciter.drive; }       void setExciterDrive(double v);
    double exciterAmount() const { return m_exciter.amount; }      void setExciterAmount(double v);
    double exciterThreshold() const { return m_exciter.thresholdDb; }
    void setExciterThreshold(double v);

    double stereoWidth() const { return m_width.width; }       void setStereoWidth(double v);
    double monoBelow() const   { return m_width.monoBelowHz; } void setMonoBelow(double v);
    double widthPhase() const  { return m_width.phaseAmount; } void setWidthPhase(double v);

    double crossfeedCutoff() const { return m_crossfeed.cutoffHz; } void setCrossfeedCutoff(double v);
    double crossfeedLevel() const  { return m_crossfeed.feedDb; }   void setCrossfeedLevel(double v);

    double lowCross() const  { return m_multiband.lowCrossHz; }  void setLowCross(double v);
    double highCross() const { return m_multiband.highCrossHz; } void setHighCross(double v);

    // Snapshots for a worker thread.
    bool tubeOn() const       { return m_tubeEnabled; }
    bool bassOn() const       { return m_bassEnabled; }
    bool dynBassOn() const    { return m_dynBassEnabled; }

    double dynBassMaxGain() const { return m_dynBass.maxGainDb; }
    void setDynBassMaxGain(double v);
    double dynBassCutoff() const { return m_dynBass.cutoffHz; }
    void setDynBassCutoff(double v);
    double dynBassRelease() const { return m_dynBass.releaseMs; }
    void setDynBassRelease(double v);
    dsp::DynamicBass::Params dynBassParams() const { return m_dynBass; }
    bool exciterOn() const    { return m_exciterEnabled; }
    bool widthOn() const      { return m_widthEnabled; }
    bool crossfeedOn() const  { return m_crossfeedEnabled; }
    bool multibandOn() const  { return m_multibandEnabled; }
    bool anyEnabled() const;

    dsp::TubeStage::Params tubeParams() const           { return m_tube; }
    dsp::VirtualBass::Params bassParams() const         { return m_bass; }
    dsp::Exciter::Params exciterParams() const          { return m_exciter; }
    dsp::StereoWidener::Params widthParams() const      { return m_width; }
    dsp::Crossfeed::Params crossfeedParams() const      { return m_crossfeed; }
    dsp::MultibandCompressor::Params multibandParams() const { return m_multiband; }

    // --- the stages added in v6 ---
    double transientAttack() const  { return m_transient.attack; }
    void setTransientAttack(double v);
    double transientSustain() const { return m_transient.sustain; }
    void setTransientSustain(double v);
    double transientAttackMs() const { return m_transient.attackMs; }
    void setTransientAttackMs(double v);
    double transientReleaseMs() const { return m_transient.releaseMs; }
    void setTransientReleaseMs(double v);

    double clipperDrive() const   { return m_clipper.driveDb; }   void setClipperDrive(double v);
    double clipperCeiling() const { return m_clipper.ceilingDb; } void setClipperCeiling(double v);
    double clipperKnee() const    { return m_clipper.kneeDb; }    void setClipperKnee(double v);
    bool clipperOversample() const { return m_clipper.oversample; }
    void setClipperOversample(bool v);

    double autoGainTarget() const { return m_autoGain.targetLufs; }  void setAutoGainTarget(double v);
    double autoGainMax() const    { return m_autoGain.maxGainDb; }   void setAutoGainMax(double v);
    double autoGainRate() const   { return m_autoGain.rateDbPerSec; } void setAutoGainRate(double v);
    double autoGainWindow() const { return m_autoGain.windowDb; }    void setAutoGainWindow(double v);

    int nightProfile() const      { return m_nightMode.profile; }       void setNightProfile(int v);
    double nightBoost() const     { return m_nightMode.boost; }         void setNightBoost(double v);
    double nightCut() const       { return m_nightMode.cut; }           void setNightCut(double v);
    double nightReference() const { return m_nightMode.referenceLufs; } void setNightReference(double v);

    bool transientOn() const { return m_transientEnabled; }
    bool clipperOn() const   { return m_clipperEnabled; }
    bool autoGainOn() const  { return m_autoGainEnabled; }
    bool nightModeOn() const { return m_nightModeEnabled; }

    dsp::TransientShaper::Params transientParams() const  { return m_transient; }
    dsp::SoftClipper::Params clipperParams() const        { return m_clipper; }
    dsp::LoudnessLeveller::Params autoGainParams() const  { return m_autoGain; }
    dsp::DynamicRange::Params nightModeParams() const     { return m_nightMode; }

    // Bulk restore, for reloading a saved session. Takes the whole block
    // because the enable bits live in its mask rather than beside each set of
    // parameters.
    void restore(const dsp::ParamBlock &b);

signals:
    void changed();

private:
    dsp::TubeStage::Params m_tube;
    dsp::VirtualBass::Params m_bass;
    dsp::DynamicBass::Params m_dynBass;
    dsp::Exciter::Params m_exciter;
    dsp::StereoWidener::Params m_width;
    dsp::Crossfeed::Params m_crossfeed;
    dsp::MultibandCompressor::Params m_multiband;
    dsp::TransientShaper::Params m_transient;
    dsp::SoftClipper::Params m_clipper;
    dsp::LoudnessLeveller::Params m_autoGain;
    dsp::DynamicRange::Params m_nightMode;

    bool m_tubeEnabled = false;
    bool m_bassEnabled = false;
    bool m_dynBassEnabled = false;
    bool m_exciterEnabled = false;
    bool m_widthEnabled = false;
    bool m_crossfeedEnabled = false;
    bool m_multibandEnabled = false;
    bool m_transientEnabled = false;
    bool m_clipperEnabled = false;
    bool m_autoGainEnabled = false;
    bool m_nightModeEnabled = false;
};

} // namespace dreamdsp
