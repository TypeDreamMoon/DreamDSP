#include "core/Biquad.h"

#include <algorithm>
#include <cmath>

namespace dreamdsp {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kButterworthQ = 0.70710678118654752; // 1/sqrt(2)
constexpr double kLinkwitzRileyQ = 0.5;

struct TypeInfo {
    const char *token;
    bool gain;
    bool q;
};

// Index-aligned with FilterType; mirrors Peace's $FilterTypes table.
constexpr TypeInfo kTypes[] = {
    { "PK",   true,  true  },
    { "LPQ",  false, true  },
    { "HPQ",  false, true  },
    { "BP",   false, true  },
    { "LS",   true,  false },
    { "HS",   true,  false },
    { "NO",   false, true  },
    { "AP",   false, true  },
    { "LSC",  true,  true  },
    { "HSC",  true,  true  },
    { "BWLP", false, true  },
    { "BWHP", false, true  },
    { "LRLP", false, true  },
    { "LRHP", false, true  },
    { "LSCQ", true,  true  },
    { "HSCQ", true,  true  },
    { "LSQ",  true,  true  },
    { "HSQ",  true,  true  },
};
static_assert(std::size(kTypes) == static_cast<size_t>(FilterType::Count),
              "kTypes must stay index-aligned with FilterType");

// Which second-order shape actually gets synthesised for a given APO type.
enum class Shape { Peak, LowShelf, HighShelf, LowPass, HighPass, BandPass, Notch, AllPass };

Shape shapeOf(FilterType t)
{
    switch (t) {
    case FilterType::PK:                                          return Shape::Peak;
    case FilterType::LS: case FilterType::LSC:
    case FilterType::LSCQ: case FilterType::LSQ:                  return Shape::LowShelf;
    case FilterType::HS: case FilterType::HSC:
    case FilterType::HSCQ: case FilterType::HSQ:                  return Shape::HighShelf;
    case FilterType::LPQ: case FilterType::BWLP:
    case FilterType::LRLP:                                        return Shape::LowPass;
    case FilterType::HPQ: case FilterType::BWHP:
    case FilterType::LRHP:                                        return Shape::HighPass;
    case FilterType::BP:                                          return Shape::BandPass;
    case FilterType::NO:                                          return Shape::Notch;
    case FilterType::AP:                                          return Shape::AllPass;
    case FilterType::Count:                                       break;
    }
    return Shape::Peak;
}

} // namespace

const char *apoToken(FilterType type)
{
    const int i = static_cast<int>(type);
    if (i < 0 || i >= static_cast<int>(FilterType::Count))
        return "PK";
    return kTypes[i].token;
}

bool hasGain(FilterType type)
{
    const int i = static_cast<int>(type);
    return (i >= 0 && i < static_cast<int>(FilterType::Count)) ? kTypes[i].gain : true;
}

bool hasQ(FilterType type)
{
    const int i = static_cast<int>(type);
    return (i >= 0 && i < static_cast<int>(FilterType::Count)) ? kTypes[i].q : true;
}

FilterType filterTypeFromToken(const QString &token, bool *ok)
{
    const QString upper = token.trimmed().toUpper();
    for (int i = 0; i < static_cast<int>(FilterType::Count); ++i) {
        if (upper == QLatin1String(kTypes[i].token)) {
            if (ok) *ok = true;
            return static_cast<FilterType>(i);
        }
    }
    // "LP"/"HP" appear in APO configs as the order-based variants; map them to
    // the Q forms so an imported configuration at least renders sensibly.
    if (ok) *ok = (upper == QLatin1String("LP") || upper == QLatin1String("HP"));
    if (upper == QLatin1String("LP")) return FilterType::LPQ;
    if (upper == QLatin1String("HP")) return FilterType::HPQ;
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

BiquadCoeffs designBiquad(FilterType type,
                          double freqHz,
                          double gainDb,
                          double q,
                          double sampleRate)
{
    BiquadCoeffs out;

    if (sampleRate <= 0.0 || freqHz <= 0.0)
        return out;

    // Types that carry no Q of their own get the textbook default; Butterworth
    // and Linkwitz-Riley have fixed alignments.
    if (!hasQ(type) || q <= 0.0)
        q = kButterworthQ;
    if (type == FilterType::LRLP || type == FilterType::LRHP)
        q = kLinkwitzRileyQ;
    if (type == FilterType::BWLP || type == FilterType::BWHP)
        q = kButterworthQ;
    if (!hasGain(type))
        gainDb = 0.0;

    // Above Nyquist the bilinear transform folds back; clamp so the curve stays
    // well-behaved instead of exploding.
    const double nyquist = sampleRate * 0.5;
    if (freqHz >= nyquist)
        freqHz = nyquist * 0.999;

    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * freqHz / sampleRate;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha = sinw0 / (2.0 * q);

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (shapeOf(type)) {
    case Shape::Peak:
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cosw0;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cosw0;
        a2 = 1.0 - alpha / A;
        break;

    case Shape::LowShelf: {
        const double sq = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + sq);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
        b2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - sq);
        a0 = (A + 1.0) + (A - 1.0) * cosw0 + sq;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
        a2 = (A + 1.0) + (A - 1.0) * cosw0 - sq;
        break;
    }

    case Shape::HighShelf: {
        const double sq = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) + (A - 1.0) * cosw0 + sq);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
        b2 = A * ((A + 1.0) + (A - 1.0) * cosw0 - sq);
        a0 = (A + 1.0) - (A - 1.0) * cosw0 + sq;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
        a2 = (A + 1.0) - (A - 1.0) * cosw0 - sq;
        break;
    }

    case Shape::LowPass:
        b0 = (1.0 - cosw0) / 2.0;
        b1 = 1.0 - cosw0;
        b2 = (1.0 - cosw0) / 2.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosw0;
        a2 = 1.0 - alpha;
        break;

    case Shape::HighPass:
        b0 = (1.0 + cosw0) / 2.0;
        b1 = -(1.0 + cosw0);
        b2 = (1.0 + cosw0) / 2.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosw0;
        a2 = 1.0 - alpha;
        break;

    case Shape::BandPass:
        b0 = alpha;
        b1 = 0.0;
        b2 = -alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosw0;
        a2 = 1.0 - alpha;
        break;

    case Shape::Notch:
        b0 = 1.0;
        b1 = -2.0 * cosw0;
        b2 = 1.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosw0;
        a2 = 1.0 - alpha;
        break;

    case Shape::AllPass:
        b0 = 1.0 - alpha;
        b1 = -2.0 * cosw0;
        b2 = 1.0 + alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosw0;
        a2 = 1.0 - alpha;
        break;
    }

    if (a0 == 0.0)
        return out;

    out.b0 = b0 / a0;
    out.b1 = b1 / a0;
    out.b2 = b2 / a0;
    out.a1 = a1 / a0;
    out.a2 = a2 / a0;
    return out;
}

double magnitudeDb(const BiquadCoeffs &c, double freqHz, double sampleRate)
{
    if (sampleRate <= 0.0)
        return 0.0;

    const double w = 2.0 * kPi * freqHz / sampleRate;
    const double cw = std::cos(w), sw = std::sin(w);
    const double c2w = std::cos(2.0 * w), s2w = std::sin(2.0 * w);

    const double numRe = c.b0 + c.b1 * cw + c.b2 * c2w;
    const double numIm = -(c.b1 * sw + c.b2 * s2w);
    const double denRe = 1.0 + c.a1 * cw + c.a2 * c2w;
    const double denIm = -(c.a1 * sw + c.a2 * s2w);

    const double num = numRe * numRe + numIm * numIm;
    const double den = denRe * denRe + denIm * denIm;
    if (den <= 0.0 || num <= 0.0)
        return -120.0;

    return 10.0 * std::log10(num / den);
}

} // namespace dreamdsp
