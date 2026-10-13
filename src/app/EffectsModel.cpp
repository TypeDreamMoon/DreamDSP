#include "app/EffectsModel.h"

#include <algorithm>
#include <cmath>

namespace dreamdsp {

namespace {
// Clamped assignment that reports whether anything moved, so every setter
// below stays a single line.
bool put(float &field, double v, double lo, double hi)
{
    if (!std::isfinite(v)) return false;
    const float nv = float(std::clamp(v, lo, hi));
    if (qFuzzyCompare(field + 1.0f, nv + 1.0f))
        return false;
    field = nv;
    return true;
}
} // namespace

EffectsModel::EffectsModel(QObject *parent)
    : QObject(parent)
{
    m_width.monoBelowHz = 120.0f;
    m_flanger.delayMs = 2.0f;
    m_flanger.feedback = 0.5f;
    // Every band starts at 1:1 so switching the multiband on changes nothing
    // until a band is actually dialled in.
    for (int b = 0; b < dsp::MultibandCompressor::kBands; ++b) {
        m_multiband.band[b].ratio = 1.0f;
        m_multiband.band[b].thresholdDb = -18.0f;
        m_multiband.band[b].kneeDb = 6.0f;
        m_multiband.band[b].attackMs = 10.0f;
        m_multiband.band[b].releaseMs = 120.0f;
    }
}

bool EffectsModel::anyEnabled() const
{
    return m_tubeEnabled || m_bassEnabled || m_exciterEnabled
           || m_widthEnabled || m_crossfeedEnabled || m_multibandEnabled
           || m_transientEnabled || m_clipperEnabled || m_autoGainEnabled
           || m_nightModeEnabled || m_dynBassEnabled || m_gateEnabled || m_dynamicEqEnabled
           || m_binauralEnabled || m_neuralEnabled || m_chorusEnabled || m_flangerEnabled || m_phaserEnabled || m_fdnEnabled || m_tapeEnabled || m_deEsserEnabled;
}

void EffectsModel::setTubeDrive(double v) { if (put(m_tube.drive, v, 1.0, 20.0)) emit changed(); }
void EffectsModel::setTubeBias(double v)  { if (put(m_tube.bias, v, 0.0, 0.8))   emit changed(); }
void EffectsModel::setTubeMix(double v)   { if (put(m_tube.mix, v, 0.0, 1.0))    emit changed(); }

void EffectsModel::setBassCutoff(double v) { if (put(m_bass.cutoffHz, v, 40.0, 250.0)) emit changed(); }
void EffectsModel::setBassAmount(double v) { if (put(m_bass.amount, v, 0.0, 1.0))      emit changed(); }
void EffectsModel::setBassDrive(double v)  { if (put(m_bass.drive, v, 1.0, 12.0))      emit changed(); }
void EffectsModel::setDynBassMaxGain(double v) { if (put(m_dynBass.maxGainDb, v, 0.0, 24.0))   emit changed(); }
void EffectsModel::setDynBassCutoff(double v)  { if (put(m_dynBass.cutoffHz, v, 30.0, 250.0))  emit changed(); }
void EffectsModel::setDynBassRelease(double v) { if (put(m_dynBass.releaseMs, v, 10.0, 2000.0)) emit changed(); }

void EffectsModel::setBassRemoveOriginal(bool v)
{
    if (m_bass.removeOriginal == v) return;
    m_bass.removeOriginal = v;
    emit changed();
}

void EffectsModel::setExciterFreq(double v)   { if (put(m_exciter.frequencyHz, v, 1000.0, 12000.0)) emit changed(); }
void EffectsModel::setExciterDrive(double v)  { if (put(m_exciter.drive, v, 1.0, 15.0))             emit changed(); }
void EffectsModel::setExciterAmount(double v) { if (put(m_exciter.amount, v, 0.0, 1.0))             emit changed(); }
void EffectsModel::setExciterThreshold(double v) { if (put(m_exciter.thresholdDb, v, -120.0, -12.0)) emit changed(); }

void EffectsModel::setStereoWidth(double v) { if (put(m_width.width, v, 0.0, 2.0))        emit changed(); }
void EffectsModel::setMonoBelow(double v)   { if (put(m_width.monoBelowHz, v, 0.0, 400.0)) emit changed(); }
void EffectsModel::setWidthPhase(double v)  { if (put(m_width.phaseAmount, v, 0.0, 1.0))   emit changed(); }

void EffectsModel::setCrossfeedCutoff(double v) { if (put(m_crossfeed.cutoffHz, v, 300.0, 1500.0)) emit changed(); }
void EffectsModel::setCrossfeedLevel(double v)  { if (put(m_crossfeed.feedDb, v, -18.0, 0.0))      emit changed(); }

void EffectsModel::setLowCross(double v)  { if (put(m_multiband.lowCrossHz, v, 60.0, 800.0))     emit changed(); }
void EffectsModel::setHighCross(double v) { if (put(m_multiband.highCrossHz, v, 1000.0, 12000.0)) emit changed(); }

// --- the stages added in v6 -------------------------------------------------

// The two shaping knobs are +/-1 because the wire format wants bounded scalars
// and because the two directions of each control are the same amount of effect
// in opposite directions -- which is how SPL's originals are marked too.
void EffectsModel::setTransientAttack(double v)  { if (put(m_transient.attack, v, -1.0, 1.0))    emit changed(); }
void EffectsModel::setTransientSustain(double v) { if (put(m_transient.sustain, v, -1.0, 1.0))   emit changed(); }
void EffectsModel::setTransientAttackMs(double v) { if (put(m_transient.attackMs, v, 1.0, 500.0)) emit changed(); }
void EffectsModel::setTransientReleaseMs(double v) { if (put(m_transient.releaseMs, v, 1.0, 5000.0)) emit changed(); }

void EffectsModel::setClipperDrive(double v)   { if (put(m_clipper.driveDb, v, 0.0, 24.0))    emit changed(); }
void EffectsModel::setClipperCeiling(double v) { if (put(m_clipper.ceilingDb, v, -24.0, 0.0)) emit changed(); }
void EffectsModel::setClipperKnee(double v)    { if (put(m_clipper.kneeDb, v, 0.0, 6.0))      emit changed(); }

void EffectsModel::setClipperOversample(bool v)
{
    if (m_clipper.oversample == v) return;
    m_clipper.oversample = v;
    emit changed();
}

void EffectsModel::setAutoGainTarget(double v) { if (put(m_autoGain.targetLufs, v, -40.0, -10.0))  emit changed(); }
void EffectsModel::setAutoGainMax(double v)    { if (put(m_autoGain.maxGainDb, v, 0.0, 25.0))      emit changed(); }
void EffectsModel::setAutoGainRate(double v)   { if (put(m_autoGain.rateDbPerSec, v, 0.25, 20.0))  emit changed(); }
void EffectsModel::setAutoGainWindow(double v) { if (put(m_autoGain.windowDb, v, 0.0, 12.0))       emit changed(); }

void EffectsModel::setNightProfile(int v)
{
    const int clamped = std::clamp(v, 0, int(dsp::DynamicRange::kProfileCount) - 1);
    if (m_nightMode.profile == clamped) return;
    m_nightMode.profile = clamped;
    emit changed();
}

void EffectsModel::setNightBoost(double v)     { if (put(m_nightMode.boost, v, 0.0, 1.0))            emit changed(); }
void EffectsModel::setNightCut(double v)       { if (put(m_nightMode.cut, v, 0.0, 1.0))              emit changed(); }
void EffectsModel::setNightReference(double v) { if (put(m_nightMode.referenceLufs, v, -40.0, -10.0)) emit changed(); }

void EffectsModel::setGateThreshold(double v) { if (put(m_gate.thresholdDb, v, -80.0, 0.0)) emit changed(); }
void EffectsModel::setGateHysteresis(double v) { if (put(m_gate.hysteresisDb, v, 0.0, 18.0)) emit changed(); }
void EffectsModel::setGateRange(double v) { if (put(m_gate.rangeDb, v, 0.0, 80.0)) emit changed(); }
void EffectsModel::setGateAttack(double v) { if (put(m_gate.attackMs, v, 0.1, 50.0)) emit changed(); }
void EffectsModel::setGateHold(double v) { if (put(m_gate.holdMs, v, 0.0, 500.0)) emit changed(); }
void EffectsModel::setGateRelease(double v) { if (put(m_gate.releaseMs, v, 10.0, 1000.0)) emit changed(); }
void EffectsModel::setDynamicEqBand(int v)
{
    const int band = std::clamp(v, 0, dsp::DynamicEq::kBands - 1);
    if (band == m_dynamicEqBand) return;
    m_dynamicEqBand = band;
    emit changed();
}
void EffectsModel::setDynamicEqBandEnabled(bool v)
{
    auto &band = m_dynamicEq.band[m_dynamicEqBand];
    if (bool(band.enabled) == v) return;
    band.enabled = v ? 1u : 0u;
    emit changed();
}
void EffectsModel::setDynamicEqFrequency(double v) { if (put(m_dynamicEq.band[m_dynamicEqBand].frequencyHz, v, 40.0, 16000.0)) emit changed(); }
void EffectsModel::setDynamicEqQ(double v) { if (put(m_dynamicEq.band[m_dynamicEqBand].q, v, 0.3, 8.0)) emit changed(); }
void EffectsModel::setDynamicEqThreshold(double v) { if (put(m_dynamicEq.band[m_dynamicEqBand].thresholdDb, v, -60.0, 0.0)) emit changed(); }
void EffectsModel::setDynamicEqRatio(double v) { if (put(m_dynamicEq.band[m_dynamicEqBand].ratio, v, 1.0, 12.0)) emit changed(); }
void EffectsModel::setDynamicEqRange(double v) { if (put(m_dynamicEq.band[m_dynamicEqBand].maxReductionDb, v, 0.0, 18.0)) emit changed(); }
void EffectsModel::setDynamicEqAttack(double v) { if (put(m_dynamicEq.band[m_dynamicEqBand].attackMs, v, 0.1, 100.0)) emit changed(); }
void EffectsModel::setDynamicEqRelease(double v) { if (put(m_dynamicEq.band[m_dynamicEqBand].releaseMs, v, 10.0, 1000.0)) emit changed(); }

void EffectsModel::setTapeDrive(double v) { if (put(m_tape.driveDb, v, 0.0, 24.0)) emit changed(); }
void EffectsModel::setTapeTone(double v) { if (put(m_tape.toneHz, v, 2000.0, 20000.0)) emit changed(); }
void EffectsModel::setTapeBump(double v) { if (put(m_tape.bumpDb, v, 0.0, 6.0)) emit changed(); }
void EffectsModel::setTapeMemory(double v) { if (put(m_tape.memory, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setTapeMix(double v) { if (put(m_tape.mix, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setTapeOutput(double v) { if (put(m_tape.outputDb, v, -24.0, 6.0)) emit changed(); }
void EffectsModel::setDeEsserFrequency(double v) { if (put(m_deEsser.frequencyHz, v, 2000.0, 12000.0)) emit changed(); }
void EffectsModel::setDeEsserThreshold(double v) { if (put(m_deEsser.thresholdDb, v, -60.0, 0.0)) emit changed(); }
void EffectsModel::setDeEsserRatio(double v) { if (put(m_deEsser.ratio, v, 1.0, 12.0)) emit changed(); }
void EffectsModel::setDeEsserRange(double v) { if (put(m_deEsser.maxReductionDb, v, 0.0, 18.0)) emit changed(); }
void EffectsModel::setDeEsserAttack(double v) { if (put(m_deEsser.attackMs, v, 0.1, 20.0)) emit changed(); }
void EffectsModel::setDeEsserRelease(double v) { if (put(m_deEsser.releaseMs, v, 10.0, 500.0)) emit changed(); }

void EffectsModel::setBinauralAngle(double v) { if (put(m_binaural.speakerAngleDeg, v, 0.0, 90.0)) emit changed(); }
void EffectsModel::setBinauralYaw(double v) { if (put(m_binaural.yawDeg, v, -180.0, 180.0)) emit changed(); }
void EffectsModel::setBinauralSpread(double v) { setBinauralAngle(v * 0.5); }
void EffectsModel::setBinauralElevation(double v)
{
    if (!std::isfinite(v)) return;
    const bool moved = put(m_binaural.elevationDeg, v, -40.0, 90.0);
    const bool switched = m_binaural.model != 1u;
    m_binaural.model = 1u; // Height needs the measured pinna cues.
    if (moved || switched) emit changed();
}
void EffectsModel::setBinauralModel(int v)
{
    const uint32_t model = v == 1 ? 1u : 0u;
    if (m_binaural.model == model) return;
    m_binaural.model = model;
    if (model == 0u) m_binaural.elevationDeg = 0.0f;
    emit changed();
}
void EffectsModel::applyCinemaSpatialPreset()
{
    // Mild room tail behind the direct speakers keeps dialogue intelligible.
    m_binaural = {32.0f, 0.0f, 1.0f, -6.0f, 8.0f, 1u};
    m_fdn = {0.75f, 5200.0f, 14.0f, 0.12f, 1.0f, 0.0f};
    m_binauralEnabled = m_fdnEnabled = true;
    m_widthEnabled = m_crossfeedEnabled = false;
    emit changed();
}
void EffectsModel::setBinauralMix(double v) { if (put(m_binaural.mix, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setBinauralOutput(double v) { if (put(m_binaural.outputDb, v, -24.0, 6.0)) emit changed(); }
void EffectsModel::setNeuralDrive(double v) { if (put(m_neural.driveDb, v, -12.0, 18.0)) emit changed(); }
void EffectsModel::setNeuralMix(double v) { if (put(m_neural.mix, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setNeuralOutput(double v) { if (put(m_neural.outputDb, v, -24.0, 6.0)) emit changed(); }
void EffectsModel::setChorusRate(double v) { if (put(m_chorus.rateHz, v, 0.02, 10.0)) emit changed(); }
void EffectsModel::setChorusDepth(double v) { if (put(m_chorus.depth, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setChorusDelay(double v) { if (put(m_chorus.delayMs, v, 0.1, 30.0)) emit changed(); }
void EffectsModel::setChorusFeedback(double v) { if (put(m_chorus.feedback, v, -0.85, 0.85)) emit changed(); }
void EffectsModel::setChorusMix(double v) { if (put(m_chorus.mix, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setChorusStereo(double v) { if (put(m_chorus.stereoPhase, v, 0.0, 0.5)) emit changed(); }
void EffectsModel::setFlangerRate(double v) { if (put(m_flanger.rateHz, v, 0.02, 10.0)) emit changed(); }
void EffectsModel::setFlangerDepth(double v) { if (put(m_flanger.depth, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setFlangerDelay(double v) { if (put(m_flanger.delayMs, v, 0.1, 5.0)) emit changed(); }
void EffectsModel::setFlangerFeedback(double v) { if (put(m_flanger.feedback, v, -0.85, 0.85)) emit changed(); }
void EffectsModel::setFlangerMix(double v) { if (put(m_flanger.mix, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setFlangerStereo(double v) { if (put(m_flanger.stereoPhase, v, 0.0, 0.5)) emit changed(); }
void EffectsModel::setPhaserRate(double v) { if (put(m_phaser.rateHz, v, 0.02, 10.0)) emit changed(); }
void EffectsModel::setPhaserDepth(double v) { if (put(m_phaser.depth, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setPhaserFeedback(double v) { if (put(m_phaser.feedback, v, -0.85, 0.85)) emit changed(); }
void EffectsModel::setPhaserMix(double v) { if (put(m_phaser.mix, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setPhaserStereo(double v) { if (put(m_phaser.stereoPhase, v, 0.0, 0.5)) emit changed(); }
void EffectsModel::setFdnDecay(double v) { if (put(m_fdn.decaySeconds, v, 0.1, 12.0)) emit changed(); }
void EffectsModel::setFdnDamping(double v) { if (put(m_fdn.dampingHz, v, 200.0, 18000.0)) emit changed(); }
void EffectsModel::setFdnPreDelay(double v) { if (put(m_fdn.preDelayMs, v, 0.0, 200.0)) emit changed(); }
void EffectsModel::setFdnMix(double v) { if (put(m_fdn.mix, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setFdnWidth(double v) { if (put(m_fdn.width, v, 0.0, 1.0)) emit changed(); }
void EffectsModel::setFdnOutput(double v) { if (put(m_fdn.outputDb, v, -24.0, 6.0)) emit changed(); }

void EffectsModel::restore(const dsp::ParamBlock &b)
{
    m_gate = b.gate;
    m_gateEnabled = (b.enableMask & dsp::kEnGate) != 0;
    m_dynamicEq = b.dynamicEq;
    m_dynamicEqEnabled = (b.enableMask & dsp::kEnDynamicEq) != 0;
    m_tape = b.tape;
    m_tapeEnabled = (b.enableMask & dsp::kEnTape) != 0;
    m_deEsser = b.deEsser;
    m_deEsserEnabled = (b.enableMask & dsp::kEnDeEsser) != 0;
    m_binaural = b.binaural;
    m_binauralEnabled = (b.enableMask & dsp::kEnBinaural) != 0;
    m_neural = b.neural;
    m_neuralEnabled = (b.enableMask & dsp::kEnNeural) != 0;
    m_chorus = b.chorus;
    m_chorusEnabled = (b.enableMask & dsp::kEnChorus) != 0;
    m_flanger = b.flanger;
    m_flangerEnabled = (b.enableMask & dsp::kEnFlanger) != 0;
    m_phaser = b.phaser;
    m_phaserEnabled = (b.enableMask & dsp::kEnPhaser) != 0;
    m_fdn = b.fdn;
    m_fdnEnabled = (b.enableMask & dsp::kEnFdn) != 0;
    m_tube = b.tube;
    m_bass = b.bass;
    m_dynBass = b.dynBass;
    m_exciter = b.exciter;
    m_width = b.width;
    m_crossfeed = b.crossfeed;
    m_multiband = b.multiband;
    m_transient = b.transient;
    m_clipper = b.clipper;
    m_autoGain = b.autoGain;
    m_nightMode = b.nightMode;

    m_tubeEnabled = (b.enableMask & dsp::kEnTube) != 0;
    m_bassEnabled = (b.enableMask & dsp::kEnBass) != 0;
    m_dynBassEnabled = (b.enableMask & dsp::kEnDynBass) != 0;
    m_exciterEnabled = (b.enableMask & dsp::kEnExciter) != 0;
    m_widthEnabled = (b.enableMask & dsp::kEnWidth) != 0;
    m_crossfeedEnabled = (b.enableMask & dsp::kEnCrossfeed) != 0;
    m_multibandEnabled = (b.enableMask & dsp::kEnMultiband) != 0;
    m_transientEnabled = (b.enableMask & dsp::kEnTransient) != 0;
    m_clipperEnabled = (b.enableMask & dsp::kEnClipper) != 0;
    m_autoGainEnabled = (b.enableMask & dsp::kEnAutoGain) != 0;
    m_nightModeEnabled = (b.enableMask & dsp::kEnNightMode) != 0;

    emit changed();
}

} // namespace dreamdsp
