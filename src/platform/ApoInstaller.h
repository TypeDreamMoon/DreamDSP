#pragma once

#include <QString>
#include <QStringList>

namespace dreamdsp {

// Installing DreamDSP's audio processing object, from inside the application.
//
// The work splits cleanly in two, and the split is what makes a one-click
// install possible without holding the user at an elevation prompt for every
// change:
//
//   machine-wide registration  -- needs elevation, done once
//   attaching to an endpoint   -- needs no elevation, done per device
//
// The second is free because BUILTIN\Users is granted SetValue on an endpoint's
// FxProperties key. See docs/apo-registration.md for the whole picture.

// What occupies an endpoint's effect slot right now.
struct ApoSlot {
    QString clsid;          // empty when the slot is free
    QString friendlyName;   // resolved via AudioEngine\AudioProcessingObjects
    bool isOurs = false;
};

// Machine-wide state. All three must hold before an endpoint attachment can do
// anything: without the engine registration in particular, the CLSID is ignored
// in complete silence.
struct ApoState {
    bool dllStaged = false;         // present in ProgramData where audiodg can read it
    bool clsidRegistered = false;   // HKLM\SOFTWARE\Classes\CLSID
    bool engineRegistered = false;  // HKLM\...\AudioEngine\AudioProcessingObjects
    bool upToDate = false;          // staged copy matches the one we shipped with
    QString stagedDll;
    QString sourceDll;              // beside the executable

    bool installed() const { return dllStaged && clsidRegistered && engineRegistered; }
};

ApoState apoState();

// The post-mix slot of one endpoint. endpointId is the full MMDevice id
// ("{0.0.0.00000000}.{guid}"); the bare guid form is accepted too.
ApoSlot apoSlotOf(const QString &endpointId);

// Attach/detach on one endpoint. No elevation required.
//
// Attaching remembers whatever CLSID it displaced, under HKCU, and detaching
// puts it back -- so an existing Equalizer APO installation survives a round
// trip. Returns an empty string on success, otherwise a message for the user.
//
// Neither takes effect until the audio service restarts.
QString attachApo(const QString &endpointId);
QString detachApo(const QString &endpointId);

// Re-runs this executable elevated with the given arguments and waits for it.
// Returns an empty string on success; the user declining the prompt is an
// ordinary, non-alarming failure.
QString runElevated(const QStringList &args);

// --- performed by the elevated copy of ourselves ---------------------------

// Stage the DLL into ProgramData, grant LOCAL SERVICE access to it, register
// the CLSID, and register with the audio engine's object database.
QString performApoInstall();

// Undo all of that. Endpoint attachments are removed separately and first.
QString performApoUninstall();

// Stop and restart Audiosrv, taking its dependent services with it. This is
// the only way a change to an endpoint's effect chain becomes audible; all
// sound on the machine cuts out for a moment.
QString restartAudioService();

// The two halves of that, because installing needs to happen *between* them.
// audiodg.exe keeps the staged DLL mapped for as long as the audio service is
// up, so replacing it means stopping the service first -- and doing that
// around the install costs one interruption instead of two restarts.
//
// stopAudioService fills `stopped` with the dependent services it had to take
// down; hand the same list back to startAudioService.
QString stopAudioService(QStringList *stopped);
QString startAudioService(const QStringList &stopped);

} // namespace dreamdsp
