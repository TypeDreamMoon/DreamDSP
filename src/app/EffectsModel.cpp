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
           || m_widthEnabled || m_crossfeedEnabled || m_multibandEnabled;
}

void EffectsModel::setTubeDrive(double v) { if (put(m_tube.drive, v, 1.0, 20.0)) emit changed(); }
void EffectsModel::setTubeBias(double v)  { if (put(m_tube.bias, v, 0.0, 0.8))   emit changed(); }
void EffectsModel::setTubeMix(double v)   { if (put(m_tube.mix, v, 0.0, 1.0))    emit changed(); }

void EffectsModel::setBassCutoff(double v) { if (put(m_bass.cutoffHz, v, 40.0, 250.0)) emit changed(); }
void EffectsModel::setBassAmount(double v) { if (put(m_bass.amount, v, 0.0, 1.0))      emit changed(); }
void EffectsModel::setBassDrive(double v)  { if (put(m_bass.drive, v, 1.0, 12.0))      emit changed(); }
void EffectsModel::setBassRemoveOriginal(bool v)
{
    if (m_bass.removeOriginal == v) return;
    m_bass.removeOriginal = v;
    emit changed();
}

void EffectsModel::setExciterFreq(double v)   { if (put(m_exciter.frequencyHz, v, 1000.0, 12000.0)) emit changed(); }
void EffectsModel::setExciterDrive(double v)  { if (put(m_exciter.drive, v, 1.0, 15.0))             emit changed(); }
void EffectsModel::setExciterAmount(double v) { if (put(m_exciter.amount, v, 0.0, 1.0))             emit changed(); }

void EffectsModel::setStereoWidth(double v) { if (put(m_width.width, v, 0.0, 2.0))        emit changed(); }
void EffectsModel::setMonoBelow(double v)   { if (put(m_width.monoBelowHz, v, 0.0, 400.0)) emit changed(); }

void EffectsModel::setCrossfeedCutoff(double v) { if (put(m_crossfeed.cutoffHz, v, 300.0, 1500.0)) emit changed(); }
void EffectsModel::setCrossfeedLevel(double v)  { if (put(m_crossfeed.feedDb, v, -18.0, 0.0))      emit changed(); }

void EffectsModel::setLowCross(double v)  { if (put(m_multiband.lowCrossHz, v, 60.0, 800.0))     emit changed(); }
void EffectsModel::setHighCross(double v) { if (put(m_multiband.highCrossHz, v, 1000.0, 12000.0)) emit changed(); }

} // namespace dreamdsp
