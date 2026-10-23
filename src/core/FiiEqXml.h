#pragma once

#include <QByteArray>
#include <QString>

#include "core/PeacePreset.h"

namespace dreamdsp {

// Reader/writer for FiiO's DSP equalizer XML -- the "<FiiO_DSP ...>" files.
//
// This is the shape a shared curve arrives in. FiiO's own DSP client (K17,
// K19) exports it, and so do the community converters that turn an AutoEQ
// result into something a FiiO device will load, which is why a file called
// "JT7 → Harman 2018.xml" is a plain preset rather than a device dump.
//
//   <FiiO_DSP model="FIIO K17 R2R" version="0.0.1">
//     <module name="EQ"><eqGroup>
//       <param name="masterGain">0</param>
//       <eqList>
//         <eq index="0">
//           <param name="type">1</param>  0 = peak, 1 = low shelf, 2 = high shelf
//           <param name="freq">47</param>
//           <param name="gain">5.4</param>
//           <param name="q">0.7</param>
//         </eq> ...
//     <styleName>JT7 → Harman 2018</styleName>
//     <description>...</description>
//
// Two encodings are in circulation. version="0.0.1" stores plain Hz/dB/Q, and
// that is what everything here reads and writes. version="1.0" -- the K19
// client's own -- stores the same fields as integers on the DSP's register
// scale (gains sit around 120, frequencies are quantised), which cannot be
// turned back into Hz/dB/Q without FiiO's tables. Those files are rejected with
// a message that says so rather than imported as numbers ten times too large,
// because a curve that is wrong by 12 dB is worse than no curve at all.
class FiiEqXml
{
public:
    // How many bands the FiiO client itself will hold.
    static constexpr int kMaxBands = 31;

    // Shelves are centre frequency + Q in these files (they come from AutoEQ,
    // which builds them that way), so Q 0.707 maps onto LSC/HSC and anything
    // else onto LSCQ/HSCQ.
    static constexpr double kStandardShelfQ = 0.70710678;

    // `model` receives the device named in the file ("FIIO K17 R2R").
    static bool read(const QString &path, Preset *out,
                     QString *model = nullptr, QString *error = nullptr);
    static bool readData(const QByteArray &data, Preset *out,
                         QString *model = nullptr, QString *error = nullptr);

    // version 0.0.1 with plain numbers, i.e. what FiiO's client and the AutoEQ
    // converters both read back.
    static bool write(const QString &path, const Preset &preset,
                      const QString &model = QString(), QString *error = nullptr);

    // Cheap check for callers that have to route a file by its contents: does
    // this look like one of these documents at all?
    static bool looksLikeFiiXml(const QByteArray &data);
};

} // namespace dreamdsp
