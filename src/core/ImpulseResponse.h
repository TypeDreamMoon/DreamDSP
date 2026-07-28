#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace dreamdsp {

// An impulse response file usable by Equalizer APO's Convolution: command.
//
// APO reads these through libsndfile (wav / flac / ogg). ViPER4Windows ships
// several hundred as ".irs", which are plain RIFF/WAVE files despite the
// extension, so they are picked up too.
struct ImpulseResponse {
    QString path;
    QString name;         // file base name
    QString category;     // immediate parent directory
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    qint64 frames = 0;
    bool readable = false;

    double durationMs() const
    {
        return (sampleRate > 0) ? (double(frames) * 1000.0 / sampleRate) : 0.0;
    }
};

// Reads just the RIFF header -- these are only ever inspected in bulk, and
// decoding hundreds of files to show a list would be absurd. Non-WAVE files
// (flac/ogg) come back with readable == false but are still usable by APO;
// they simply cannot be pre-checked for sample rate.
bool readWaveHeader(const QString &path, ImpulseResponse *out);

// Recursively collects impulse responses under `roots`.
QVector<ImpulseResponse> scanImpulseResponses(const QStringList &roots, int limit = 4000);

// Directories worth looking in, in priority order: DreamDSP's own, APO's config
// directory, and a ViPER4Windows installation if one is present.
QStringList defaultImpulseRoots(const QString &apoConfigPath, const QString &userDir);

} // namespace dreamdsp
