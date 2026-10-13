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

    Q_PROPERTY(bool binauralEnabled MEMBER m_binauralEnabled NOTIFY changed)
    Q_PROPERTY(double binauralAngle READ binauralAngle WRITE setBinauralAngle NOTIFY changed)
    Q_PROPERTY(double binauralYaw READ binauralYaw WRITE setBinauralYaw NOTIFY changed)
    Q_PROPERTY(double binauralSpread READ binauralSpread WRITE setBinauralSpread NOTIFY changed)
    Q_PROPERTY(double binauralRotation READ binauralRotation WRITE setBinauralYaw NOTIFY changed)
    Q_PROPERTY(double binauralElevation READ binauralElevation WRITE setBinauralElevation NOTIFY changed)
    Q_PROPERTY(int binauralModel READ binauralModel WRITE setBinauralModel NOTIFY changed)
    Q_PROPERTY(double binauralMix READ binauralMix WRITE setBinauralMix NOTIFY changed)
    Q_PROPERTY(double binauralOutput READ binauralOutput WRITE setBinauralOutput NOTIFY changed)
    Q_PROPERTY(bool neuralEnabled MEMBER m_neuralEnabled NOTIFY changed)
    Q_PROPERTY(double neuralDrive READ neuralDrive WRITE setNeuralDrive NOTIFY changed)
    Q_PROPERTY(double neuralMix READ neuralMix WRITE setNeuralMix NOTIFY changed)
    Q_PROPERTY(double neuralOutput READ neuralOutput WRITE setNeuralOutput NOTIFY changed)
    Q_PROPERTY(bool chorusEnabled MEMBER m_chorusEnabled NOTIFY changed)
    Q_PROPERTY(double chorusRate READ chorusRate WRITE setChorusRate NOTIFY changed)
    Q_PROPERTY(double chorusDepth READ chorusDepth WRITE setChorusDepth NOTIFY changed)
    Q_PROPERTY(double chorusDelay READ chorusDelay WRITE setChorusDelay NOTIFY changed)
    Q_PROPERTY(double chorusFeedback READ chorusFeedback WRITE setChorusFeedback NOTIFY changed)
    Q_PROPERTY(double chorusMix READ chorusMix WRITE setChorusMix NOTIFY changed)
    Q_PROPERTY(double chorusStereo READ chorusStereo WRITE setChorusStereo NOTIFY changed)
    Q_PROPERTY(bool flangerEnabled MEMBER m_flangerEnabled NOTIFY changed)
    Q_PROPERTY(double flangerRate READ flangerRate WRITE setFlangerRate NOTIFY changed)
    Q_PROPERTY(double flangerDepth READ flangerDepth WRITE setFlangerDepth NOTIFY changed)
    Q_PROPERTY(double flangerDelay READ flangerDelay WRITE setFlangerDelay NOTIFY changed)
    Q_PROPERTY(double flangerFeedback READ flangerFeedback WRITE setFlangerFeedback NOTIFY changed)
    Q_PROPERTY(double flangerMix READ flangerMix WRITE setFlangerMix NOTIFY changed)
    Q_PROPERTY(double flangerStereo READ flangerStereo WRITE setFlangerStereo NOTIFY changed)
    Q_PROPERTY(bool phaserEnabled MEMBER m_phaserEnabled NOTIFY changed)
    Q_PROPERTY(double phaserRate READ phaserRate WRITE setPhaserRate NOTIFY changed)
    Q_PROPERTY(double phaserDepth READ phaserDepth WRITE setPhaserDepth NOTIFY changed)
    Q_PROPERTY(double phaserFeedback READ phaserFeedback WRITE setPhaserFeedback NOTIFY changed)
    Q_PROPERTY(double phaserMix READ phaserMix WRITE setPhaserMix NOTIFY changed)
    Q_PROPERTY(double phaserStereo READ phaserStereo WRITE setPhaserStereo NOTIFY changed)
    Q_PROPERTY(bool fdnEnabled MEMBER m_fdnEnabled NOTIFY changed)
    Q_PROPERTY(double fdnDecay READ fdnDecay WRITE setFdnDecay NOTIFY changed)
    Q_PROPERTY(double fdnDamping READ fdnDamping WRITE setFdnDamping NOTIFY changed)
    Q_PROPERTY(double fdnPreDelay READ fdnPreDelay WRITE setFdnPreDelay NOTIFY changed)
    Q_PROPERTY(double fdnMix READ fdnMix WRITE setFdnMix NOTIFY changed)
    Q_PROPERTY(double fdnWidth READ fdnWidth WRITE setFdnWidth NOTIFY changed)
    Q_PROPERTY(double fdnOutput READ fdnOutput WRITE setFdnOutput NOTIFY changed)

    Q_PROPERTY(bool tapeEnabled MEMBER m_tapeEnabled NOTIFY changed)
    Q_PROPERTY(double tapeDrive READ tapeDrive WRITE setTapeDrive NOTIFY changed)
    Q_PROPERTY(double tapeTone READ tapeTone WRITE setTapeTone NOTIFY changed)
    Q_PROPERTY(double tapeBump READ tapeBump WRITE setTapeBump NOTIFY changed)
    Q_PROPERTY(double tapeMemory READ tapeMemory WRITE setTapeMemory NOTIFY changed)
    Q_PROPERTY(double tapeMix READ tapeMix WRITE setTapeMix NOTIFY changed)
    Q_PROPERTY(double tapeOutput READ tapeOutput WRITE setTapeOutput NOTIFY changed)
    Q_PROPERTY(bool deEsserEnabled MEMBER m_deEsserEnabled NOTIFY changed)
    Q_PROPERTY(double deEsserFrequency READ deEsserFrequency WRITE setDeEsserFrequency NOTIFY changed)
    Q_PROPERTY(double deEsserThreshold READ deEsserThreshold WRITE setDeEsserThreshold NOTIFY changed)
    Q_PROPERTY(double deEsserRatio READ deEsserRatio WRITE setDeEsserRatio NOTIFY changed)
    Q_PROPERTY(double deEsserRange READ deEsserRange WRITE setDeEsserRange NOTIFY changed)
    Q_PROPERTY(double deEsserAttack READ deEsserAttack WRITE setDeEsserAttack NOTIFY changed)
    Q_PROPERTY(double deEsserRelease READ deEsserRelease WRITE setDeEsserRelease NOTIFY changed)

    Q_PROPERTY(bool gateEnabled MEMBER m_gateEnabled NOTIFY changed)
    Q_PROPERTY(double gateThreshold READ gateThreshold WRITE setGateThreshold NOTIFY changed)
    Q_PROPERTY(double gateHysteresis READ gateHysteresis WRITE setGateHysteresis NOTIFY changed)
    Q_PROPERTY(double gateRange READ gateRange WRITE setGateRange NOTIFY changed)
    Q_PROPERTY(double gateAttack READ gateAttack WRITE setGateAttack NOTIFY changed)
    Q_PROPERTY(double gateHold READ gateHold WRITE setGateHold NOTIFY changed)
    Q_PROPERTY(double gateRelease READ gateRelease WRITE setGateRelease NOTIFY changed)
    Q_PROPERTY(bool dynamicEqEnabled MEMBER m_dynamicEqEnabled NOTIFY changed)
    Q_PROPERTY(int dynamicEqBand READ dynamicEqBand WRITE setDynamicEqBand NOTIFY changed)
    Q_PROPERTY(bool dynamicEqBandEnabled READ dynamicEqBandEnabled WRITE setDynamicEqBandEnabled NOTIFY changed)
    Q_PROPERTY(double dynamicEqFrequency READ dynamicEqFrequency WRITE setDynamicEqFrequency NOTIFY changed)
    Q_PROPERTY(double dynamicEqDefaultFrequency READ dynamicEqDefaultFrequency NOTIFY changed)
    Q_PROPERTY(double dynamicEqQ READ dynamicEqQ WRITE setDynamicEqQ NOTIFY changed)
    Q_PROPERTY(double dynamicEqThreshold READ dynamicEqThreshold WRITE setDynamicEqThreshold NOTIFY changed)
    Q_PROPERTY(double dynamicEqRatio READ dynamicEqRatio WRITE setDynamicEqRatio NOTIFY changed)
    Q_PROPERTY(double dynamicEqRange READ dynamicEqRange WRITE setDynamicEqRange NOTIFY changed)
    Q_PROPERTY(double dynamicEqAttack READ dynamicEqAttack WRITE setDynamicEqAttack NOTIFY changed)
    Q_PROPERTY(double dynamicEqRelease READ dynamicEqRelease WRITE setDynamicEqRelease NOTIFY changed)

public:
    explicit EffectsModel(QObject *parent = nullptr);

    // The value this control would have had out of the box, for the reset
    // button beside it. Empty for a name that is not a property of this model.
    Q_INVOKABLE QVariant defaultOf(const QString &name) const
    {
        if (name == QStringLiteral("dynamicEqFrequency")) return dynamicEqDefaultFrequency();
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

    bool binauralOn() const { return m_binauralEnabled; }
    dsp::Binaural::Params binauralParams() const { return m_binaural; }
    double binauralAngle() const { return m_binaural.speakerAngleDeg; } void setBinauralAngle(double v);
    double binauralYaw() const { return m_binaural.yawDeg; } void setBinauralYaw(double v);
    // The DSP uses positive-left azimuth: (+/- halfSpread - yaw). In a
    // clockwise, head-fixed view the speaker ring therefore rotates by +yaw.
    double binauralRotation() const { return m_binaural.yawDeg; }
    double binauralSpread() const { return 2.0 * m_binaural.speakerAngleDeg; }
    void setBinauralSpread(double v);
    double binauralElevation() const { return m_binaural.elevationDeg; }
    void setBinauralElevation(double v);
    int binauralModel() const { return int(m_binaural.model); }
    void setBinauralModel(int v);
    void applyCinemaSpatialPreset();
    double binauralMix() const { return m_binaural.mix; } void setBinauralMix(double v);
    double binauralOutput() const { return m_binaural.outputDb; } void setBinauralOutput(double v);
    bool neuralOn() const { return m_neuralEnabled; }
    dsp::NeuralSaturation::Params neuralParams() const { return m_neural; }
    double neuralDrive() const { return m_neural.driveDb; } void setNeuralDrive(double v);
    double neuralMix() const { return m_neural.mix; } void setNeuralMix(double v);
    double neuralOutput() const { return m_neural.outputDb; } void setNeuralOutput(double v);
    bool chorusOn() const { return m_chorusEnabled; }
    dsp::Modulation::Params chorusParams() const { return m_chorus; }
    double chorusRate() const { return m_chorus.rateHz; } void setChorusRate(double v);
    double chorusDepth() const { return m_chorus.depth; } void setChorusDepth(double v);
    double chorusDelay() const { return m_chorus.delayMs; } void setChorusDelay(double v);
    double chorusFeedback() const { return m_chorus.feedback; } void setChorusFeedback(double v);
    double chorusMix() const { return m_chorus.mix; } void setChorusMix(double v);
    double chorusStereo() const { return m_chorus.stereoPhase; } void setChorusStereo(double v);
    bool flangerOn() const { return m_flangerEnabled; }
    dsp::Modulation::Params flangerParams() const { return m_flanger; }
    double flangerRate() const { return m_flanger.rateHz; } void setFlangerRate(double v);
    double flangerDepth() const { return m_flanger.depth; } void setFlangerDepth(double v);
    double flangerDelay() const { return m_flanger.delayMs; } void setFlangerDelay(double v);
    double flangerFeedback() const { return m_flanger.feedback; } void setFlangerFeedback(double v);
    double flangerMix() const { return m_flanger.mix; } void setFlangerMix(double v);
    double flangerStereo() const { return m_flanger.stereoPhase; } void setFlangerStereo(double v);
    bool phaserOn() const { return m_phaserEnabled; }
    dsp::Modulation::Params phaserParams() const { return m_phaser; }
    double phaserRate() const { return m_phaser.rateHz; } void setPhaserRate(double v);
    double phaserDepth() const { return m_phaser.depth; } void setPhaserDepth(double v);
    double phaserFeedback() const { return m_phaser.feedback; } void setPhaserFeedback(double v);
    double phaserMix() const { return m_phaser.mix; } void setPhaserMix(double v);
    double phaserStereo() const { return m_phaser.stereoPhase; } void setPhaserStereo(double v);
    bool fdnOn() const { return m_fdnEnabled; }
    dsp::FdnReverb::Params fdnParams() const { return m_fdn; }
    double fdnDecay() const { return m_fdn.decaySeconds; } void setFdnDecay(double v);
    double fdnDamping() const { return m_fdn.dampingHz; } void setFdnDamping(double v);
    double fdnPreDelay() const { return m_fdn.preDelayMs; } void setFdnPreDelay(double v);
    double fdnMix() const { return m_fdn.mix; } void setFdnMix(double v);
    double fdnWidth() const { return m_fdn.width; } void setFdnWidth(double v);
    double fdnOutput() const { return m_fdn.outputDb; } void setFdnOutput(double v);

    bool tapeOn() const { return m_tapeEnabled; }
    dsp::TapeSaturation::Params tapeParams() const { return m_tape; }
    double tapeDrive() const { return m_tape.driveDb; } void setTapeDrive(double v);
    double tapeTone() const { return m_tape.toneHz; } void setTapeTone(double v);
    double tapeBump() const { return m_tape.bumpDb; } void setTapeBump(double v);
    double tapeMemory() const { return m_tape.memory; } void setTapeMemory(double v);
    double tapeMix() const { return m_tape.mix; } void setTapeMix(double v);
    double tapeOutput() const { return m_tape.outputDb; } void setTapeOutput(double v);
    bool deEsserOn() const { return m_deEsserEnabled; }
    dsp::DeEsser::Params deEsserParams() const { return m_deEsser; }
    double deEsserFrequency() const { return m_deEsser.frequencyHz; } void setDeEsserFrequency(double v);
    double deEsserThreshold() const { return m_deEsser.thresholdDb; } void setDeEsserThreshold(double v);
    double deEsserRatio() const { return m_deEsser.ratio; } void setDeEsserRatio(double v);
    double deEsserRange() const { return m_deEsser.maxReductionDb; } void setDeEsserRange(double v);
    double deEsserAttack() const { return m_deEsser.attackMs; } void setDeEsserAttack(double v);
    double deEsserRelease() const { return m_deEsser.releaseMs; } void setDeEsserRelease(double v);

    bool gateOn() const { return m_gateEnabled; }
    dsp::NoiseGate::Params gateParams() const { return m_gate; }
    double gateThreshold() const { return m_gate.thresholdDb; } void setGateThreshold(double v);
    double gateHysteresis() const { return m_gate.hysteresisDb; } void setGateHysteresis(double v);
    double gateRange() const { return m_gate.rangeDb; } void setGateRange(double v);
    double gateAttack() const { return m_gate.attackMs; } void setGateAttack(double v);
    double gateHold() const { return m_gate.holdMs; } void setGateHold(double v);
    double gateRelease() const { return m_gate.releaseMs; } void setGateRelease(double v);
    bool dynamicEqOn() const { return m_dynamicEqEnabled; }
    dsp::DynamicEq::Params dynamicEqParams() const { return m_dynamicEq; }
    int dynamicEqBand() const { return m_dynamicEqBand; } void setDynamicEqBand(int v);
    bool dynamicEqBandEnabled() const { return selectedDynamicBand().enabled != 0; }
    void setDynamicEqBandEnabled(bool v);
    double dynamicEqFrequency() const { return selectedDynamicBand().frequencyHz; } void setDynamicEqFrequency(double v);
    double dynamicEqDefaultFrequency() const { return dsp::DynamicEq::Params{}.band[m_dynamicEqBand].frequencyHz; }
    double dynamicEqQ() const { return selectedDynamicBand().q; } void setDynamicEqQ(double v);
    double dynamicEqThreshold() const { return selectedDynamicBand().thresholdDb; } void setDynamicEqThreshold(double v);
    double dynamicEqRatio() const { return selectedDynamicBand().ratio; } void setDynamicEqRatio(double v);
    double dynamicEqRange() const { return selectedDynamicBand().maxReductionDb; } void setDynamicEqRange(double v);
    double dynamicEqAttack() const { return selectedDynamicBand().attackMs; } void setDynamicEqAttack(double v);
    double dynamicEqRelease() const { return selectedDynamicBand().releaseMs; } void setDynamicEqRelease(double v);

    // Bulk restore, for reloading a saved session. Takes the whole block
    // because the enable bits live in its mask rather than beside each set of
    // parameters.
    void restore(const dsp::ParamBlock &b);

signals:
    void changed();

private:
    const dsp::DynamicEq::Band &selectedDynamicBand() const { return m_dynamicEq.band[m_dynamicEqBand]; }
    dsp::NoiseGate::Params m_gate;
    bool m_gateEnabled = false;
    dsp::DynamicEq::Params m_dynamicEq;
    bool m_dynamicEqEnabled = false;
    int m_dynamicEqBand = 0; // GUI selection only; all three bands remain in the snapshot.
    dsp::TapeSaturation::Params m_tape;
    bool m_tapeEnabled = false;
    dsp::DeEsser::Params m_deEsser;
    bool m_deEsserEnabled = false;
    dsp::Binaural::Params m_binaural;
    bool m_binauralEnabled = false;
    dsp::NeuralSaturation::Params m_neural;
    bool m_neuralEnabled = false;
    dsp::Modulation::Params m_chorus;
    bool m_chorusEnabled = false;
    dsp::Modulation::Params m_flanger;
    bool m_flangerEnabled = false;
    dsp::Modulation::Params m_phaser;
    bool m_phaserEnabled = false;
    dsp::FdnReverb::Params m_fdn;
    bool m_fdnEnabled = false;
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
