#include "app/ReverbModel.h"

#include <algorithm>

namespace dreamdsp {

namespace {
// Sets `field` to a clamped `v` and reports whether it actually moved, so each
// setter stays one line instead of six.
bool assign(float &field, double v, double lo, double hi)
{
    const float nv = float(std::clamp(v, lo, hi));
    if (qFuzzyCompare(field + 1.0f, nv + 1.0f))
        return false;
    field = nv;
    return true;
}
} // namespace

ReverbModel::ReverbModel(QObject *parent)
    : QObject(parent)
{
    m_p.roomSize = 0.6f;
    m_p.damping = 0.5f;
    m_p.density = 0.5f;
    m_p.bandwidth = 0.85f;
    m_p.preDelayMs = 20.0f;
    m_p.width = 1.0f;
    m_p.wet = 0.25f;
    m_p.dry = 1.0f;
}

void ReverbModel::setEnabled(bool v)
{
    if (m_enabled == v) return;
    m_enabled = v; emit paramsChanged();
}

void ReverbModel::setRoomSize(double v)  { if (assign(m_p.roomSize, v, 0.0, 1.0))    emit paramsChanged(); }
void ReverbModel::setDamping(double v)   { if (assign(m_p.damping, v, 0.0, 1.0))     emit paramsChanged(); }
void ReverbModel::setDensity(double v)   { if (assign(m_p.density, v, 0.0, 1.0))     emit paramsChanged(); }
void ReverbModel::setBandwidth(double v) { if (assign(m_p.bandwidth, v, 0.0, 1.0))   emit paramsChanged(); }
void ReverbModel::setPreDelay(double v)  { if (assign(m_p.preDelayMs, v, 0.0, 200.0)) emit paramsChanged(); }
void ReverbModel::setWidth(double v)     { if (assign(m_p.width, v, 0.0, 1.0))       emit paramsChanged(); }
void ReverbModel::setWet(double v)       { if (assign(m_p.wet, v, 0.0, 1.0))         emit paramsChanged(); }
void ReverbModel::setDry(double v)       { if (assign(m_p.dry, v, 0.0, 1.0))         emit paramsChanged(); }

} // namespace dreamdsp
