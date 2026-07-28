#pragma once

#include <QString>
#include <QVector>

#include "core/Biquad.h"

namespace dreamdsp {

struct PresetBand {
    double frequency = 1000.0;
    double gainDb = 0.0;
    double q = 1.41;
    FilterType type = FilterType::PK;
    bool enabled = true;
};

struct Preset {
    QString name;
    QString description;
    double preamp = 0.0;
    QVector<PresetBand> bands;

    bool isValid() const { return !bands.isEmpty(); }
};

// Reader/writer for Peace's ".peace" preset files.
//
// Only the "All speakers" slot is handled -- the sections without a numeric
// suffix ([Frequencies], [Gains], [Qualities], [Filters], [Disabled]). Peace
// additionally stores per-speaker copies in [Frequencies1..8] etc.; those are
// preserved on import only in the sense that we ignore them, and a file we
// write mirrors slot 0 into slot 1..8 so Peace can still open it.
//
// Sparse sections are the norm: [Gains] is omitted entirely when every band is
// at 0 dB, [Filters] only lists bands that are not PK, and [Disabled] only
// lists disabled bands.
class PeaceFile
{
public:
    static bool read(const QString &path, Preset *out, QString *error = nullptr);
    static bool write(const QString &path, const Preset &preset, QString *error = nullptr);
};

} // namespace dreamdsp
