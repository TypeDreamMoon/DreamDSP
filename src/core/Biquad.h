#pragma once

#include <QString>

#include "Equalizer.h"   // dsp layer

// Qt-side spelling of the filter vocabulary.
//
// The types, the design formulas and the magnitude response all live in
// dsp/Equalizer.h now, because that is the copy that runs inside audiodg.exe.
// This header exists only to add the string handling Qt makes convenient and
// the DSP layer must not depend on.
//
// It used to hold a second implementation of the same maths, which meant the
// curve drawn on screen and the filter applied to the audio came from two
// pieces of code that were merely intended to agree. They did not: this file's
// Butterworth and Linkwitz-Riley types were single second-order sections, and
// nothing applied them at all.

namespace dreamdsp {

using FilterType = dsp::FilterKind;
using BiquadCoeffs = dsp::BiquadCoeffs;
using FilterSections = dsp::FilterSections;

using dsp::magnitudeDb;

const char *apoToken(FilterType type);
bool hasGain(FilterType type);
bool hasQ(FilterType type);

// Parse an APO token ("PK", "LSCQ", ...). Returns PK for anything unknown.
FilterType filterTypeFromToken(const QString &token, bool *ok = nullptr);

// Peace stores the type as this enum's integer value.
FilterType filterTypeFromIndex(int index, bool *ok = nullptr);
inline int filterTypeIndex(FilterType type) { return static_cast<int>(type); }

// One filter, as up to two cascaded sections. Butterworth and Linkwitz-Riley
// are fourth order and use both.
FilterSections designBiquad(FilterType type,
                            double freqHz,
                            double gainDb,
                            double q,
                            double sampleRate);

} // namespace dreamdsp
