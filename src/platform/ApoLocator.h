#pragma once

#include <QString>

namespace dreamdsp {

// Where Equalizer APO lives on this machine.
//
// ConfigPath MUST be read from the registry rather than derived from
// InstallPath: APO's installer only rewrites ConfigPath when InstallPath
// changes, so on upgraded machines the two can point at different places.
struct ApoInstall {
    bool found = false;
    QString installPath;   // e.g. "D:/Program Files/EqualizerAPO"
    QString configPath;    // e.g. "D:/Program Files/EqualizerAPO/config"
    QString version;       // file version of EqualizerAPO.dll, e.g. "1.4.1.0"
    bool configWritable = false;
};

ApoInstall locateApo();

// Absolute path of a file inside APO's config directory.
QString configFilePath(const ApoInstall &apo, const QString &fileName);

} // namespace dreamdsp
