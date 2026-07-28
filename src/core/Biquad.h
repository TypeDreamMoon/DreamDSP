#pragma once

#include <QString>

// Biquad design + magnitude response, used to render the predicted frequency
// response and to emit Equalizer APO filter lines.
//
// Formulas follow the Audio EQ Cookbook (Robert Bristow-Johnson).

namespace dreamdsp {

// The 18 filter types Equalizer APO accepts, in the exact order Peace stores
// them as integers in a .peace file's [Filters] section -- so the enum value
// IS the on-disk index. Do not reorder.
enum class FilterType {
    PK = 0,   // peaking
    LPQ,      // low-pass with Q
    HPQ,      // high-pass with Q
    BP,       // band-pass
    LS,       // low shelf  (no Q)
    HS,       // high shelf (no Q)
    NO,       // notch
    AP,       // all-pass
    LSC,      // low shelf,  centre frequency
    HSC,      // high shelf, centre frequency
    BWLP,     // Butterworth low-pass
    BWHP,     // Butterworth high-pass
    LRLP,     // Linkwitz-Riley low-pass
    LRHP,     // Linkwitz-Riley high-pass
    LSCQ,     // low shelf,  centre frequency, with Q
    HSCQ,     // high shelf, centre frequency, with Q
    LSQ,      // low shelf  with Q
    HSQ,      // high shelf with Q
    Count
};

const char *apoToken(FilterType type);
bool hasGain(FilterType type);
bool hasQ(FilterType type);

// Parse an APO token ("PK", "LSCQ", ...). Returns PK for anything unknown.
FilterType filterTypeFromToken(const QString &token, bool *ok = nullptr);

// Peace stores the type as this enum's integer value.
FilterType filterTypeFromIndex(int index, bool *ok = nullptr);
inline int filterTypeIndex(FilterType type) { return static_cast<int>(type); }

// Normalised coefficients: a0 is folded into the others.
struct BiquadCoeffs {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

// Butterworth and Linkwitz-Riley types are higher-order in APO; here they are
// approximated by the equivalent second-order section, which is close enough
// for a preview curve but is NOT what APO actually applies.
BiquadCoeffs designBiquad(FilterType type,
                          double freqHz,
                          double gainDb,
                          double q,
                          double sampleRate);

// Magnitude of H(e^jw) in dB at freqHz.
double magnitudeDb(const BiquadCoeffs &c, double freqHz, double sampleRate);

} // namespace dreamdsp
