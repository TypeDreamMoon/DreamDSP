#include "ui/ImpulseCurveItem.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace dreamdsp {

namespace {

// The decade marks worth labelling. Anything denser is unreadable at the size
// this is drawn.
const struct { double hz; const char *label; } kGrid[] = {
    { 50.0, "50" },     { 100.0, "100" },   { 500.0, "500" },
    { 1000.0, "1k" },   { 5000.0, "5k" },   { 10000.0, "10k" },
};

} // namespace

ImpulseCurveItem::ImpulseCurveItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void ImpulseCurveItem::setCurve(const QVector<float> &c)
{
    if (m_curve == c)
        return;
    m_curve = c;
    emit curveChanged();
    update();
}

void ImpulseCurveItem::setMinFreq(double v)
{
    if (qFuzzyCompare(m_minFreq, v))
        return;
    m_minFreq = v;
    emit rangeChanged();
    update();
}

void ImpulseCurveItem::setMaxFreq(double v)
{
    if (qFuzzyCompare(m_maxFreq, v))
        return;
    m_maxFreq = v;
    emit rangeChanged();
    update();
}

void ImpulseCurveItem::setFloorDb(double v)
{
    if (qFuzzyCompare(m_floorDb, v))
        return;
    m_floorDb = v;
    emit rangeChanged();
    update();
}

void ImpulseCurveItem::setCurveColor(const QColor &c)
{
    if (m_curveColor == c)
        return;
    m_curveColor = c;
    emit colorsChanged();
    update();
}

void ImpulseCurveItem::setGridColor(const QColor &c)
{
    if (m_gridColor == c)
        return;
    m_gridColor = c;
    emit colorsChanged();
    update();
}

void ImpulseCurveItem::setLabelColor(const QColor &c)
{
    if (m_labelColor == c)
        return;
    m_labelColor = c;
    emit colorsChanged();
    update();
}

void ImpulseCurveItem::paint(QPainter *painter)
{
    const double w = width();
    const double h = height();
    if (w <= 1.0 || h <= 1.0 || m_maxFreq <= m_minFreq)
        return;

    painter->setRenderHint(QPainter::Antialiasing, true);

    const double decades = std::log(m_maxFreq / m_minFreq);
    const auto xForHz = [&](double hz) {
        return w * std::log(hz / m_minFreq) / decades;
    };
    // The top of the plot is 0 dB -- the curve is referred to its own peak, so
    // nothing goes above it -- with a little headroom so the line is not
    // clipped against the edge.
    const double topDb = 2.0;
    const auto yForDb = [&](double db) {
        return h * (topDb - db) / (topDb - m_floorDb);
    };

    // --- grid --------------------------------------------------------------
    QPen gridPen(m_gridColor);
    gridPen.setWidthF(1.0);
    painter->setPen(gridPen);

    for (const auto &g : kGrid) {
        if (g.hz <= m_minFreq || g.hz >= m_maxFreq)
            continue;
        const double x = xForHz(g.hz);
        painter->drawLine(QPointF(x, 0.0), QPointF(x, h));
    }
    for (double db = 0.0; db > m_floorDb; db -= 10.0) {
        const double y = yForDb(db);
        painter->drawLine(QPointF(0.0, y), QPointF(w, y));
    }

    // --- labels ------------------------------------------------------------
    QFont f = painter->font();
    f.setPixelSize(9);
    painter->setFont(f);
    painter->setPen(m_labelColor);
    for (const auto &g : kGrid) {
        if (g.hz <= m_minFreq || g.hz >= m_maxFreq)
            continue;
        painter->drawText(QRectF(xForHz(g.hz) + 3.0, h - 13.0, 40.0, 12.0),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QString::fromLatin1(g.label));
    }

    if (m_curve.size() < 2)
        return;

    // --- curve -------------------------------------------------------------
    QPainterPath path;
    for (int i = 0; i < m_curve.size(); ++i) {
        const double t = double(i) / double(m_curve.size() - 1);
        const double x = w * t;
        const double y = yForDb(std::max(double(m_curve[i]), m_floorDb));
        if (i == 0)
            path.moveTo(x, y);
        else
            path.lineTo(x, y);
    }

    // Filled down to the floor as well as stroked: the shape of the area is
    // easier to compare between two responses than a thin line is.
    QPainterPath filled = path;
    filled.lineTo(w, h);
    filled.lineTo(0.0, h);
    filled.closeSubpath();

    QColor fill = m_curveColor;
    fill.setAlphaF(0.16f);
    painter->fillPath(filled, fill);

    QPen curvePen(m_curveColor);
    curvePen.setWidthF(1.6);
    curvePen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(curvePen);
    painter->drawPath(path);
}

} // namespace dreamdsp
