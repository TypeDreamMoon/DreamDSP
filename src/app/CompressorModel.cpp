#include "app/CompressorModel.h"

#include <algorithm>

namespace dreamdsp {

CompressorModel::CompressorModel(QObject *parent)
    : QObject(parent)
{
    m_p.thresholdDb = -18.0f;
    m_p.ratio = 4.0f;
    m_p.kneeDb = 6.0f;
    m_p.attackMs = 5.0f;
    m_p.releaseMs = 80.0f;
    m_p.makeupDb = 0.0f;
    m_comp.prepare(48000.0, 2);
    apply();
}

void CompressorModel::apply()
{
    m_comp.setParams(m_p);
    emit paramsChanged();
}

void CompressorModel::setEnabled(bool v)
{
    if (m_enabled == v) return;
    m_enabled = v; emit paramsChanged();
}

void CompressorModel::setThreshold(double v)
{
    v = std::clamp(v, -60.0, 0.0);
    if (qFuzzyCompare(m_p.thresholdDb + 1.0f, float(v) + 1.0f)) return;
    m_p.thresholdDb = float(v); apply();
}

void CompressorModel::setRatio(double v)
{
    v = std::clamp(v, 1.0, 100.0);
    if (qFuzzyCompare(m_p.ratio + 1.0f, float(v) + 1.0f)) return;
    m_p.ratio = float(v); apply();
}

void CompressorModel::setKnee(double v)
{
    v = std::clamp(v, 0.0, 24.0);
    if (qFuzzyCompare(m_p.kneeDb + 1.0f, float(v) + 1.0f)) return;
    m_p.kneeDb = float(v); apply();
}

void CompressorModel::setAttack(double v)
{
    v = std::clamp(v, 0.1, 200.0);
    if (qFuzzyCompare(m_p.attackMs + 1.0f, float(v) + 1.0f)) return;
    m_p.attackMs = float(v); apply();
}

void CompressorModel::setRelease(double v)
{
    v = std::clamp(v, 5.0, 2000.0);
    if (qFuzzyCompare(m_p.releaseMs + 1.0f, float(v) + 1.0f)) return;
    m_p.releaseMs = float(v); apply();
}

void CompressorModel::setMakeup(double v)
{
    v = std::clamp(v, -12.0, 24.0);
    if (qFuzzyCompare(m_p.makeupDb + 1.0f, float(v) + 1.0f)) return;
    m_p.makeupDb = float(v); apply();
}

void CompressorModel::setAutoKnee(bool v)    { if (m_p.autoKnee == v) return;    m_p.autoKnee = v; apply(); }
void CompressorModel::setAutoAttack(bool v)  { if (m_p.autoAttack == v) return;  m_p.autoAttack = v; apply(); }
void CompressorModel::setAutoRelease(bool v) { if (m_p.autoRelease == v) return; m_p.autoRelease = v; apply(); }
void CompressorModel::setAutoMakeup(bool v)  { if (m_p.autoMakeup == v) return;  m_p.autoMakeup = v; apply(); }

// Both come straight from the DSP object rather than being re-derived here --
// a second copy of the formula is a second thing to get wrong.
double CompressorModel::effectiveKnee() const   { return m_comp.effectiveKneeDb(); }
double CompressorModel::effectiveMakeup() const { return m_comp.effectiveMakeupDb(); }

double CompressorModel::outputFor(double inputDb) const
{
    return m_comp.outputForInputDb(float(inputDb));
}

void CompressorModel::restore(const dsp::Compressor::Params &p, bool enabled)
{
    m_p = p;
    m_enabled = enabled;
    // apply(), not a bare signal: the DSP object has to be told too.
    //
    // Without it the model's parameters and m_comp's disagree, and everything
    // derived from m_comp keeps answering from the old ones -- the transfer
    // curve, and the resolved knee and makeup shown while the automatic modes
    // are on. Those are the only place those numbers exist, so a restore
    // looked exactly like the settings had been lost.
    apply();
}

} // namespace dreamdsp
