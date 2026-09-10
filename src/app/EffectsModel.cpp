#include "app/EffectsModel.h"

#include <algorithm>

namespace dreamdsp {

namespace {
// Clamped assignment that reports whether anything moved, so every setter
// below stays a single line.
bool put(float &field, double v, double lo, double hi)
{
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
           || m_nightModeEnabled;
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

void EffectsModel::restore(const dsp::ParamBlock &b)
{
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
