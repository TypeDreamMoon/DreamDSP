#pragma once

#include <QString>

#include "ParamBlock.h"
#include "core/PeacePreset.h"

namespace dreamdsp {

// A preset that holds everything, not just the equalizer.
//
// The .peace format is Peace's, and it can only describe filter bands -- which
// is the whole of what Peace does. Half of DreamDSP is not expressible in it:
// the effect rack, the convolution, which impulse response, what the AutoEQ
// import came from. Saving a "preset" that silently dropped all of that would
// be worse than not offering one.
//
// So this is a second format alongside it rather than a replacement. Saving as
// .peace still works and still opens in Peace; saving as .dreamdsp keeps the
// rest as well.
//
// JSON, and readable JSON at that. A preset is something people swap, edit by
// hand and diff, and a base64 blob of the wire struct would have been shorter
// and useless for all three.
struct DreamPreset {
    QString name;

    Preset eq;                      // preamp and bands
    bool eqEnabled = true;

    // The effect rack and the convolution scalars, in exactly the form the DSP
    // consumes them.
    dsp::ParamBlock params = dsp::transparentBlock();

    // The impulse response is referred to by path rather than embedded. A
    // preset stays a small text file, and the file it points at is usually one
    // of the hundreds already installed.
    QString convolutionFile;
    QString convolutionName;

    // What an AutoEQ import came from, for the record. Purely informational --
    // the bands it produced are already in `eq`.
    QString autoEqSource;
};

bool saveDreamPreset(const QString &path, const DreamPreset &preset, QString *error = nullptr);

// Missing fields take the value they would have in a freshly-constructed
// preset, so a file written by an older version still loads.
bool loadDreamPreset(const QString &path, DreamPreset *out, QString *error = nullptr);

} // namespace dreamdsp
