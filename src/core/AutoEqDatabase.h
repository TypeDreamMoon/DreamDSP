#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "core/PeacePreset.h"

namespace dreamdsp {

// Reader for the compressed AutoEQ databases that ship with Peace and sit in
// Equalizer APO's config directory (AutoEQCompressedHarman5.txt, ...IEF5,
// ...IEFBass5, OPRAEQCompressed5).
//
// Format, from Peace.au3:593-601 -- one record per line, the first character
// being a tag:
//
//   <version> <count>        first line, e.g. "5.1 8853"
//   M<name>                  measurer, applies to following entries
//   W<name>                  target curve / measurement rig
//   D<name>                  headphone; starts a new entry
//   A...                     an EQ line (see below)
//   R<v>R<v>...              raw response curve  (ignored here)
//   O<path>                  OPRA source path    (ignored here)
//
// An EQ line is a preamp followed by filters, all run together:
//
//   A-6 M105 G5.5 Q0.7 P180 G-3.3 Q0.57 ... I10000 G-1.1 Q0.7
//
//   A = preamp    P = peaking          F = peaking on a fixed frequency
//   L = low shelf H = high shelf       M = low shelf, centre frequency
//   I = high shelf, centre frequency
//   G = gain      Q = quality
//
// 'E' is deliberately never used as a tag, because it would be read as an
// exponent when the following digits are converted to a number.
struct AutoEqEntry {
    QString device;
    QString measurer;
    QString target;
    QString parametric;   // the "A..." line with P/L/H/M/I filters
    QString fixedBand;    // the "A...F...F..." line, if present
    int source = 0;       // index into AutoEqDatabase::sourceNames()

    QString displayName() const;
};

class AutoEqDatabase
{
public:
    // Databases are ~20 MB of text in total, so loading is explicit rather than
    // happening on construction.
    //
    // Searched in order, first match per database wins. DreamDSP's own data
    // directory is one of them, so the feature does not require Peace or
    // Equalizer APO to be installed to reach the files they ship.
    bool load(const QStringList &dirs, QString *error = nullptr);
    bool loaded() const { return m_loaded; }

    const QVector<AutoEqEntry> &entries() const { return m_entries; }
    static QStringList sourceNames();

    // Case-insensitive substring match on device/measurer/target.
    QVector<int> search(const QString &needle, int limit = 200) const;

    // Turns an entry into bands. `fixedBand` picks the 10-band graphic variant
    // (fixed frequencies, Q 1.41) over the parametric one.
    static bool toPreset(const AutoEqEntry &entry, bool fixedBand, Preset *out);

private:
    bool loadFile(const QString &path, int sourceIndex, QString *error);

    QVector<AutoEqEntry> m_entries;
    bool m_loaded = false;
};

} // namespace dreamdsp
