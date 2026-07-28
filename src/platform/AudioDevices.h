#pragma once

#include <QString>
#include <QVector>

namespace dreamdsp {

struct AudioDevice {
    QString id;        // MMDevice endpoint ID, e.g. "{0.0.0.00000000}.{guid}"
    QString name;      // friendly name
    bool isDefault = false;
    bool active = true;
};

// Render (playback) endpoints. Includes unplugged/disabled devices so that a
// configuration bound to a currently-absent device is still selectable.
QVector<AudioDevice> enumerateRenderDevices(QString *error = nullptr);

} // namespace dreamdsp
