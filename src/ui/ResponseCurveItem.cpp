#include "ui/ResponseCurveItem.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace dreamdsp {

namespace {

constexpr double kFMin = 20.0;
constexpr double kFMax = 20000.0;
constexpr double kSampleRate = 48000.0;

// Frequencies that get a labelled gridline.
const double kGridFreqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };

QString shortHz(double hz)
{
    if (hz >= 1000.0)
        return QStringLiteral("%1k").arg(hz / 1000.0, 0, 'g', 2);
    return QString::number(hz, 'f', 0);
}

double freqToX(double hz, double w)
{
    const double t = std::log10(hz / kFMin) / std::log10(kFMax / kFMin);
    return t * w;
}

} // namespace

ResponseCurveItem::ResponseCurveItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
}

void ResponseCurveItem::setBands(EqBandModel *m)
{
    if (m_bands == m)
        return;
    if (m_bands)
        disconnect(m_bands, nullptr, this, nullptr);
    m_bands = m;
    if (m_bands)
        connect(m_bands, &EqBandModel::bandsChanged, this, [this] { update(); });
    emit bandsChanged();
    update();
}

void ResponseCurveItem::setPreamp(double v)
{
    if (qFuzzyCompare(m_preamp + 1.0, v + 1.0))
        return;
    m_preamp = v;
    emit preampChanged();
    update();
}

void ResponseCurveItem::setRangeDb(double v)
{
    if (v <= 0.0 || qFuzzyCompare(m_rangeDb + 1.0, v + 1.0))
        return;
    m_rangeDb = v;
    emit rangeDbChanged();
    update();
}

void ResponseCurveItem::setCurveColor(const QColor &c)
{
    if (m_curveColor == c) return;
    m_curveColor = c; emit curveColorChanged(); update();
}

void ResponseCurveItem::setGridColor(const QColor &c)
{
    if (m_gridColor == c) return;
    m_gridColor = c; emit gridColorChanged(); update();
}

void ResponseCurveItem::setLabelColor(const QColor &c)
{
    if (m_labelColor == c) return;
    m_labelColor = c; emit labelColorChanged(); update();
}

void ResponseCurveItem::setFilled(bool v)
{
    if (m_filled == v) return;
    m_filled = v; emit filledChanged(); update();
}

void ResponseCurveItem::setSpectrum(const QVector<float> &s)
{
    m_spectrum = s;

    // Fall slowly, rise instantly. A raw 20 fps analyser is unreadable strobing;
    // the decay is what makes it legible without hiding transients.
    if (m_decay.size() != s.size())
        m_decay = s.isEmpty() ? QVector<float>() : QVector<float>(s.size(), -90.0f);
    for (int i = 0; i < s.size(); ++i)
        m_decay[i] = std::max(s[i], m_decay[i] - 2.5f);

    emit spectrumChanged();
    update();
}

void ResponseCurveItem::setSpectrumColor(const QColor &c)
{
    if (m_spectrumColor == c) return;
    m_spectrumColor = c; emit spectrumColorChanged(); update();
}

void ResponseCurveItem::paint(QPainter *painter)
{
    const double w = width();
    const double h = height();
    if (w <= 1.0 || h <= 1.0)
        return;

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const auto dbToY = [&](double db) {
        const double t = (m_rangeDb - db) / (2.0 * m_rangeDb);
        return t * h;
    };

    // --- live spectrum, behind everything ---------------------------------
    // Bands are log-spaced over exactly the same 20 Hz .. 20 kHz range as the
    // frequency axis, so band index maps straight to x with no conversion.
    if (!m_decay.isEmpty()) {
        constexpr double kSpecFloor = -80.0;   // dBFS at the bottom of the plot
        constexpr double kSpecTop = 0.0;

        const int n = m_decay.size();
        QPainterPath spec;
        spec.moveTo(0.0, h);
        for (int i = 0; i < n; ++i) {
            const double x = (double(i) + 0.5) / double(n) * w;
            const double t = std::clamp((double(m_decay[i]) - kSpecFloor) / (kSpecTop - kSpecFloor),
                                        0.0, 1.0);
            spec.lineTo(x, h - t * h);
        }
        spec.lineTo(w, h);
        spec.closeSubpath();

        QLinearGradient sg(0, 0, 0, h);
        QColor top = m_spectrumColor;  top.setAlpha(115);
        QColor bot = m_spectrumColor;  bot.setAlpha(18);
        sg.setColorAt(0.0, top);
        sg.setColorAt(1.0, bot);
        painter->fillPath(spec, sg);
    }

    // --- grid -------------------------------------------------------------
    QPen gridPen(m_gridColor);
    gridPen.setWidthF(1.0);
    painter->setPen(gridPen);

    for (const double f : kGridFreqs) {
        const double x = freqToX(f, w);
        painter->drawLine(QPointF(x, 0.0), QPointF(x, h));
    }

    const double dbStep = (m_rangeDb > 12.0) ? 6.0 : 3.0;
    for (double db = -m_rangeDb; db <= m_rangeDb + 0.001; db += dbStep) {
        const double y = dbToY(db);
        if (std::abs(db) < 0.001)
            continue; // zero line drawn separately, brighter
        painter->drawLine(QPointF(0.0, y), QPointF(w, y));
    }

    QPen zeroPen(m_gridColor);
    zeroPen.setWidthF(1.0);
    zeroPen.setColor(QColor(m_gridColor.red(), m_gridColor.green(), m_gridColor.blue(),
                            std::min(255, m_gridColor.alpha() * 3)));
    painter->setPen(zeroPen);
    painter->drawLine(QPointF(0.0, dbToY(0.0)), QPointF(w, dbToY(0.0)));

    // --- labels -----------------------------------------------------------
    QFont f = painter->font();
    f.setPixelSize(10);
    painter->setFont(f);
    painter->setPen(m_labelColor);
    for (const double freq : kGridFreqs) {
        if (freq == kFMin || freq == kFMax)
            continue;
        const double x = freqToX(freq, w);
        painter->drawText(QRectF(x - 20.0, h - 14.0, 40.0, 12.0),
                          Qt::AlignHCenter | Qt::AlignVCenter, shortHz(freq));
    }
    painter->drawText(QRectF(2.0, 1.0, 40.0, 12.0), Qt::AlignLeft | Qt::AlignVCenter,
                      QStringLiteral("+%1").arg(m_rangeDb, 0, 'f', 0));
    painter->drawText(QRectF(2.0, h - 26.0, 40.0, 12.0), Qt::AlignLeft | Qt::AlignVCenter,
                      QStringLiteral("-%1").arg(m_rangeDb, 0, 'f', 0));

    if (!m_bands)
        return;

    // --- curve ------------------------------------------------------------
    // Precompute coefficients once, then evaluate per pixel column.
    QVector<BiquadCoeffs> coeffs;
    coeffs.reserve(m_bands->bands().size());
    for (const PresetBand &b : m_bands->bands()) {
        if (!b.enabled)
            continue;
        // A gain-less type (LP/HP/BP/NO) still shapes the curve at 0 dB, so
        // only gain-carrying bands may be skipped for being flat.
        if (hasGain(b.type) && qFuzzyIsNull(b.gainDb))
            continue;
        coeffs.push_back(designBiquad(b.type, b.frequency, b.gainDb, b.q, kSampleRate));
    }

    const int columns = std::max(2, static_cast<int>(w));
    QPainterPath path;
    for (int i = 0; i < columns; ++i) {
        const double t = double(i) / double(columns - 1);
        const double hz = kFMin * std::pow(kFMax / kFMin, t);

        double db = m_preamp;
        for (const BiquadCoeffs &c : std::as_const(coeffs))
            db += magnitudeDb(c, hz, kSampleRate);

        const double x = t * w;
        const double y = dbToY(std::clamp(db, -m_rangeDb - 5.0, m_rangeDb + 5.0));
        if (i == 0)
            path.moveTo(x, y);
        else
            path.lineTo(x, y);
    }

    if (m_filled) {
        QPainterPath fill = path;
        fill.lineTo(w, dbToY(0.0));
        fill.lineTo(0.0, dbToY(0.0));
        fill.closeSubpath();

        QLinearGradient g(0, 0, 0, h);
        QColor top = m_curveColor;   top.setAlpha(90);
        QColor mid = m_curveColor;   mid.setAlpha(10);
        g.setColorAt(0.0, top);
        g.setColorAt(0.5, mid);
        g.setColorAt(1.0, top);
        painter->fillPath(fill, g);
    }

    QPen curvePen(m_curveColor);
    curvePen.setWidthF(2.0);
    curvePen.setJoinStyle(Qt::RoundJoin);
    curvePen.setCapStyle(Qt::RoundCap);
    painter->strokePath(path, curvePen);
}

} // namespace dreamdsp
