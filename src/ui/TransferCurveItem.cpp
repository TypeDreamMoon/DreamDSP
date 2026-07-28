#include "ui/TransferCurveItem.h"

#include "app/CompressorModel.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace dreamdsp {

namespace {
constexpr double kMinDb = -60.0;
constexpr double kMaxDb = 6.0;
}

TransferCurveItem::TransferCurveItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void TransferCurveItem::setModel(CompressorModel *m)
{
    if (m_model == m)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (m_model)
        connect(m_model, &CompressorModel::paramsChanged, this, [this] { update(); });
    emit modelChanged();
    update();
}

void TransferCurveItem::setCurveColor(const QColor &c)
{
    if (m_curveColor == c) return;
    m_curveColor = c; emit curveColorChanged(); update();
}

void TransferCurveItem::setGridColor(const QColor &c)
{
    if (m_gridColor == c) return;
    m_gridColor = c; emit gridColorChanged(); update();
}

void TransferCurveItem::setLabelColor(const QColor &c)
{
    if (m_labelColor == c) return;
    m_labelColor = c; emit labelColorChanged(); update();
}

void TransferCurveItem::paint(QPainter *painter)
{
    const double w = width();
    const double h = height();
    if (w <= 1.0 || h <= 1.0)
        return;

    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto toX = [&](double db) { return (db - kMinDb) / (kMaxDb - kMinDb) * w; };
    const auto toY = [&](double db) { return h - (db - kMinDb) / (kMaxDb - kMinDb) * h; };

    // Grid every 12 dB on both axes.
    painter->setPen(QPen(m_gridColor, 1.0));
    for (double db = kMinDb; db <= kMaxDb + 0.01; db += 12.0) {
        painter->drawLine(QPointF(toX(db), 0.0), QPointF(toX(db), h));
        painter->drawLine(QPointF(0.0, toY(db)), QPointF(w, toY(db)));
    }

    // Unity reference: where the curve would sit with no compression at all.
    QPen unity(m_gridColor);
    unity.setStyle(Qt::DashLine);
    unity.setWidthF(1.0);
    painter->setPen(unity);
    painter->drawLine(QPointF(toX(kMinDb), toY(kMinDb)), QPointF(toX(kMaxDb), toY(kMaxDb)));

    painter->setPen(m_labelColor);
    QFont f = painter->font();
    f.setPixelSize(10);
    painter->setFont(f);
    painter->drawText(QRectF(4, h - 15, 60, 12), Qt::AlignLeft, QStringLiteral("-60 dB 输入"));
    painter->drawText(QRectF(w - 64, 3, 60, 12), Qt::AlignRight, QStringLiteral("输出 0 dB"));

    if (!m_model)
        return;

    // Threshold marker.
    const double thr = m_model->threshold();
    QPen thrPen(m_curveColor);
    thrPen.setStyle(Qt::DotLine);
    thrPen.setWidthF(1.0);
    painter->setPen(thrPen);
    painter->setOpacity(0.5);
    painter->drawLine(QPointF(toX(thr), 0.0), QPointF(toX(thr), h));
    painter->setOpacity(1.0);

    QPainterPath path;
    const int steps = std::max(2, int(w));
    for (int i = 0; i < steps; ++i) {
        const double in = kMinDb + (kMaxDb - kMinDb) * double(i) / double(steps - 1);
        const double out = std::clamp(m_model->outputFor(in), kMinDb, kMaxDb);
        const QPointF p(toX(in), toY(out));
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }

    QPen curve(m_curveColor);
    curve.setWidthF(2.0);
    curve.setJoinStyle(Qt::RoundJoin);
    painter->strokePath(path, curve);
}

} // namespace dreamdsp
