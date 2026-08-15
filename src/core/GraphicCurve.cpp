#include "core/GraphicCurve.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace dreamdsp {

GraphicCurve parseGraphicCurve(const QString &text)
{
    GraphicCurve out;

    QString body = text;
    const int colon = body.indexOf(QLatin1Char(':'));
    if (colon >= 0 && body.left(colon).trimmed().compare(QStringLiteral("GraphicEQ"),
                                                         Qt::CaseInsensitive) == 0) {
        body = body.mid(colon + 1);
    }
    body.replace(QLatin1Char(';'), QLatin1Char('\n'));
    // Locale decimal commas, as elsewhere in this project. Done after the
    // semicolon split so a comma cannot be read as a separator.
    body.replace(QLatin1Char(','), QLatin1Char('.'));

    static const QRegularExpression separator(QStringLiteral("\\s+"));
    const QStringList lines = body.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1Char('#')))
            continue;
        const QStringList parts = t.split(separator, Qt::SkipEmptyParts);
        if (parts.size() < 2)
            continue;
        bool okHz = false, okDb = false;
        const double hz = parts.at(0).toDouble(&okHz);
        const double db = parts.at(1).toDouble(&okDb);
        if (okHz && okDb && hz > 0.0)
            out.append({ hz, db });
    }

    std::sort(out.begin(), out.end(),
              [](const QPair<double, double> &a, const QPair<double, double> &b) {
                  return a.first < b.first;
              });

    // Two points at the same frequency would make the interpolation below
    // divide by a zero-width interval. The last one wins, which is what a
    // hand-edited file that repeats a line means.
    for (int i = out.size() - 1; i > 0; --i) {
        if (out.at(i).first == out.at(i - 1).first)
            out.remove(i - 1);
    }
    return out;
}

void resampleCurve(const GraphicCurve &curve, const double *frequencies, int count,
                   double *gainsOut)
{
    if (!frequencies || !gainsOut || count <= 0)
        return;

    for (int i = 0; i < count; ++i) {
        const double hz = frequencies[i];
        if (curve.isEmpty()) {
            gainsOut[i] = 0.0;
            continue;
        }
        if (hz <= curve.first().first) {
            gainsOut[i] = curve.first().second;
            continue;
        }
        if (hz >= curve.last().first) {
            gainsOut[i] = curve.last().second;
            continue;
        }
        int hi = 1;
        while (hi < curve.size() && curve.at(hi).first < hz)
            ++hi;
        const auto &a = curve.at(hi - 1);
        const auto &b = curve.at(hi);
        const double t = (std::log(hz) - std::log(a.first))
                         / (std::log(b.first) - std::log(a.first));
        gainsOut[i] = a.second + t * (b.second - a.second);
    }
}

} // namespace dreamdsp
