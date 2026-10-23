#pragma once

#include <QString>

#include "core/PeacePreset.h"

namespace dreamdsp {

// Reader for the parametric text every EQ front end exports: AutoEQ's
// "ParametricEQ.txt", Equalizer APO's filter lines, and the tables reviewers
// publish in their reviews. One filter per line, in either of these shapes:
//
//   Preamp: -3.0 dB
//   Filter 1: ON LSC Fc 105 Hz Gain 3.0 dB Q 0.70
//   Filter 2: ON PK  Fc 2957 Hz Gain -5.2 dB Q 4.24
//
// A cone of "frequency gain" pairs is *not* this format -- that is a GraphicEQ
// curve and belongs to core/GraphicCurve.h. The two are told apart by the
// "Filter" keyword, so a dropped file can be routed by its content rather than
// by its name.
class ParametricEqText
{
public:
    static bool looksLikeParametricEq(const QString &text);

    // Fails only when nothing usable was found, so a caller can try the graphic
    // curve reader next without having to undo anything.
    static bool read(const QString &text, Preset *out, QString *error = nullptr);
};

} // namespace dreamdsp
