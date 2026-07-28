#pragma once

#include <QString>

namespace dreamdsp::autostart {

// Run-at-login via HKCU\...\CurrentVersion\Run. User scope, so no elevation --
// and it survives without a scheduled task or a shortcut in the Startup folder.
//
// The registered command carries --tray so an automatic launch starts hidden.
bool isEnabled();
bool setEnabled(bool on, QString *error = nullptr);

// The exact command line that is (or would be) registered.
QString command();

} // namespace dreamdsp::autostart
