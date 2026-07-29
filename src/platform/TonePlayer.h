#pragma once

#include <QString>

namespace dreamdsp {

// Renders a test tone to one specific endpoint via WASAPI shared mode.
//
// Needed because a render stream is the only thing that makes the audio engine
// build a device's effect chain. Loopback capture taps the mix but does not
// instantiate APOs, so it cannot be used to find out whether one loaded.
//
// Blocking; intended for the diagnostic command line, not the GUI.
// Returns an empty string on success, otherwise a description of the failure.
QString playTone(const QString &endpointId, double seconds, double frequencyHz,
                 double amplitude = 0.2);

} // namespace dreamdsp
