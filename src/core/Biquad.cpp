#include "core/Biquad.h"

namespace dreamdsp {

const char *apoToken(FilterType type)
{
    return dsp::filterToken(type);
}

bool hasGain(FilterType type)
{
    return dsp::filterHasGain(type);
}

bool hasQ(FilterType type)
{
    return dsp::filterHasQ(type);
}

FilterType filterTypeFromToken(const QString &token, bool *ok)
{
    const QString upper = token.trimmed().toUpper();
    for (int i = 0; i < static_cast<int>(FilterType::Count); ++i) {
        if (upper == QLatin1String(dsp::filterToken(static_cast<FilterType>(i)))) {
            if (ok) *ok = true;
            return static_cast<FilterType>(i);
        }
    }

    // Equalizer APO's own spelling for the passes, and the one AutoEQ and Room
    // EQ Wizard exports use. Q is carried separately, so these are our Q forms.
    if (upper == QLatin1String("LP")) { if (ok) *ok = true; return FilterType::LPQ; }
    if (upper == QLatin1String("HP")) { if (ok) *ok = true; return FilterType::HPQ; }
    // APO accepts all three of these for a peaking filter.
    if (upper == QLatin1String("PEQ") || upper == QLatin1String("MODAL")) {
        if (ok) *ok = true;
        return FilterType::PK;
    }

    if (ok) *ok = false;
    return FilterType::PK;
}

FilterType filterTypeFromIndex(int index, bool *ok)
{
    if (index >= 0 && index < static_cast<int>(FilterType::Count)) {
        if (ok) *ok = true;
        return static_cast<FilterType>(index);
    }
    if (ok) *ok = false;
    return FilterType::PK;
}

FilterSections designBiquad(FilterType type,
                            double freqHz,
                            double gainDb,
                            double q,
                            double sampleRate)
{
    return dsp::designFilter(type, freqHz, gainDb, q, sampleRate);
}

} // namespace dreamdsp
