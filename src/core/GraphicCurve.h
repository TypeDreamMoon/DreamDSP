#pragma once

#include <QPair>
#include <QString>
#include <QVector>

// Reader for the magnitude curves AutoEQ publishes and JamesDSP calls an
// arbitrary-response equalizer.
//
// In core/ rather than inside the controller because it is a file format, and
// a file format that cannot be tested without starting an application is a file
// format that does not get tested.

namespace dreamdsp {

// (frequency Hz, gain dB), sorted by frequency, duplicates collapsed.
using GraphicCurve = QVector<QPair<double, double>>;

// Accepts both forms these files come in:
//
//   GraphicEQ: 20 -1.5; 25 -1.4; 31.5 -1.3; ...
//   20 -1.5
//   25 -1.4
//
// Anything unparseable is skipped rather than failing the whole file: these are
// hand-edited as often as generated, and one bad line should not lose the rest.
GraphicCurve parseGraphicCurve(const QString &text);

// Resamples onto `count` frequencies, interpolating in log frequency -- the
// axis the points were chosen on, and the only one where a straight line
// between two of them means what the author intended.
//
// Outside the data the curve is held flat rather than extrapolated. A
// correction measured to 20 kHz says nothing about what is above it, and a
// slope carried on past the last point is an invention.
void resampleCurve(const GraphicCurve &curve, const double *frequencies, int count,
                   double *gainsOut);

} // namespace dreamdsp
