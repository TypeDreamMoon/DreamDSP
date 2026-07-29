#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "app/EqBandModel.h"
#include "app/ParamPublisher.h"
#include "app/PresetStore.h"
#include "core/AutoEqDatabase.h"
#include "core/ImpulseResponse.h"
#include "platform/ApoInstaller.h"
#include "platform/ApoLocator.h"
#include "platform/AudioDevices.h"
#include "platform/HotkeyManager.h"
#include "platform/LoopbackCapture.h"
#include "platform/PeakMeter.h"
#include "platform/TrayIcon.h"

#include <QHash>
#include <QVariantList>

namespace dreamdsp {

// The single façade QML talks to.
//
// DreamDSP deliberately writes its own include file (dreamdsp.txt) and only ever
// adds/removes one line in config.txt. That keeps an existing Peace or manual
// setup intact and makes "engaging" reversible.
class AppController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool apoFound READ apoFound NOTIFY apoChanged)
    Q_PROPERTY(QString apoVersion READ apoVersion NOTIFY apoChanged)
    Q_PROPERTY(QString apoConfigPath READ apoConfigPath NOTIFY apoChanged)
    Q_PROPERTY(bool configWritable READ configWritable NOTIFY apoChanged)

    Q_PROPERTY(QStringList deviceNames READ deviceNames NOTIFY devicesChanged)
    Q_PROPERTY(int currentDevice READ currentDevice WRITE setCurrentDevice NOTIFY currentDeviceChanged)

    Q_PROPERTY(dreamdsp::EqBandModel *bands READ bands CONSTANT)
    Q_PROPERTY(dreamdsp::PresetStore *presets READ presets CONSTANT)

    // The effect models live here rather than in the QML that displays them.
    // They used to be created by the effects pane, which meant they did not
    // exist until that pane was first shown and were destroyed with it -- so
    // nothing with a process-long lifetime could save them, restore them, or
    // notice a change in order to publish it.
    Q_PROPERTY(dreamdsp::CompressorModel *compressor READ compressor CONSTANT)
    Q_PROPERTY(dreamdsp::ReverbModel *reverb READ reverbModel CONSTANT)
    Q_PROPERTY(dreamdsp::EffectsModel *effects READ effects CONSTANT)
    Q_PROPERTY(QString currentPreset READ currentPreset NOTIFY currentPresetChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

    Q_PROPERTY(double preamp READ preamp WRITE setPreamp NOTIFY preampChanged)
    Q_PROPERTY(bool eqEnabled READ eqEnabled WRITE setEqEnabled NOTIFY eqEnabledChanged)
    Q_PROPERTY(bool engaged READ engaged WRITE setEngaged NOTIFY engagedChanged)

    Q_PROPERTY(QString generatedText READ generatedText NOTIFY generatedTextChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY lastMessageChanged)

    Q_PROPERTY(bool convolutionEnabled READ convolutionEnabled WRITE setConvolutionEnabled NOTIFY convolutionChanged)
    Q_PROPERTY(QString convolutionFile READ convolutionFile NOTIFY convolutionChanged)
    Q_PROPERTY(QString convolutionName READ convolutionName NOTIFY convolutionChanged)
    Q_PROPERTY(int convolutionRate READ convolutionRate NOTIFY convolutionChanged)
    Q_PROPERTY(int deviceRate READ deviceRate NOTIFY convolutionChanged)
    Q_PROPERTY(bool rateMismatch READ rateMismatch NOTIFY convolutionChanged)
    Q_PROPERTY(int impulseCount READ impulseCount NOTIFY impulsesChanged)

    Q_PROPERTY(bool spectrumEnabled READ spectrumEnabled WRITE setSpectrumEnabled NOTIFY spectrumEnabledChanged)
    Q_PROPERTY(QVector<float> spectrum READ spectrum NOTIFY spectrumChanged)
    Q_PROPERTY(bool spectrumLive READ spectrumLive NOTIFY spectrumChanged)

    Q_PROPERTY(double peakLevel READ peakLevel NOTIFY peakLevelChanged)
    Q_PROPERTY(bool meterAvailable READ meterAvailable NOTIFY peakLevelChanged)
    Q_PROPERTY(bool metering READ metering WRITE setMetering NOTIFY meteringChanged)
    Q_PROPERTY(bool perDevice READ perDevice WRITE setPerDevice NOTIFY perDeviceChanged)
    Q_PROPERTY(bool autoEqLoaded READ autoEqLoaded NOTIFY autoEqChanged)
    Q_PROPERTY(int autoEqCount READ autoEqCount NOTIFY autoEqChanged)

    // --- the DreamDSP APO, i.e. actually being in the audio path ----------
    Q_PROPERTY(bool apoInstalled READ apoInstalled NOTIFY apoStateChanged)
    Q_PROPERTY(bool apoUpToDate READ apoUpToDate NOTIFY apoStateChanged)
    Q_PROPERTY(bool apoAttached READ apoAttached NOTIFY apoStateChanged)
    Q_PROPERTY(QString apoSlotOwner READ apoSlotOwner NOTIFY apoStateChanged)
    Q_PROPERTY(bool apoBusy READ apoBusy NOTIFY apoStateChanged)
    Q_PROPERTY(bool apoRestartPending READ apoRestartPending NOTIFY apoStateChanged)

    // What the copy running inside audiodg says it is doing. Without this a
    // slider that appears to do nothing has half a dozen indistinguishable
    // explanations, and the user has no way to tell which one they have.
    Q_PROPERTY(bool apoLive READ apoLive NOTIFY apoStatusChanged)
    Q_PROPERTY(bool apoInSync READ apoInSync NOTIFY apoStatusChanged)
    Q_PROPERTY(QString apoStatusText READ apoStatusText NOTIFY apoStatusChanged)

    Q_PROPERTY(bool trayActive READ trayActive NOTIFY trayActiveChanged)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY autostartChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged)
    Q_PROPERTY(bool startHidden READ startHidden CONSTANT)
    Q_PROPERTY(bool quitting READ quitting NOTIFY quittingChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    bool apoFound() const { return m_apo.found; }
    QString apoVersion() const { return m_apo.version; }
    QString apoConfigPath() const { return m_apo.configPath; }
    bool configWritable() const { return m_apo.configWritable; }

    QStringList deviceNames() const { return m_deviceNames; }
    int currentDevice() const { return m_currentDevice; }
    void setCurrentDevice(int index);

    EqBandModel *bands() { return &m_bands; }
    PresetStore *presets() { return &m_presets; }
    CompressorModel *compressor() { return &m_compressor; }
    ReverbModel *reverbModel() { return &m_reverb; }
    EffectsModel *effects() { return &m_effects; }

    // True when an APO instance is streaming somewhere on this machine.
    bool apoLive() const;
    // True when what it is running is the parameter set last published.
    bool apoInSync() const;
    QString apoStatusText() const;
    QString currentPreset() const { return m_currentPreset; }
    bool dirty() const { return m_dirty; }

    double preamp() const { return m_preamp; }
    void setPreamp(double db);
    bool eqEnabled() const { return m_eqEnabled; }
    void setEqEnabled(bool on);
    bool engaged() const { return m_engaged; }
    void setEngaged(bool on);

    QString generatedText() const;
    QString lastError() const { return m_lastError; }
    QString lastMessage() const { return m_lastMessage; }

    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE void openConfigFolder();
    Q_INVOKABLE void openPresetFolder();
    Q_INVOKABLE void flushNow();

    Q_INVOKABLE void loadPreset(int row);
    Q_INVOKABLE bool savePreset(const QString &name);
    Q_INVOKABLE bool deletePreset(int row);

    // --- the DreamDSP APO -------------------------------------------------
    //
    // Two separable steps, which is why there are two states rather than one
    // "installed" flag: registering the object machine-wide needs elevation and
    // happens once, attaching it to a particular endpoint does not and happens
    // per device. Neither becomes audible until the audio service restarts.
    bool apoInstalled() const { return m_apoState.installed(); }
    bool apoUpToDate() const { return m_apoState.upToDate; }
    bool apoAttached() const { return m_apoSlot.isOurs; }
    // Who holds the current device's post-mix slot: empty when free, our own
    // name when attached, otherwise the product that would be displaced.
    QString apoSlotOwner() const { return m_apoSlot.friendlyName; }
    bool apoBusy() const { return m_apoBusy; }
    // Set once a change has been made that only takes effect after a restart.
    bool apoRestartPending() const { return m_apoRestartPending; }

    Q_INVOKABLE void refreshApoState();
    Q_INVOKABLE void installApo();
    Q_INVOKABLE void uninstallApo();
    Q_INVOKABLE void setApoAttached(bool on);
    Q_INVOKABLE void restartAudio();

    bool trayActive() const;
    bool autostart() const;
    void setAutostart(bool on);
    bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool on);
    // True when launched by the run-at-login entry, which passes --tray.
    bool startHidden() const { return m_startHidden; }

    // Set once the application is genuinely on its way out.
    //
    // Since Qt 6.6 quit() asks every top-level window to close first and gives
    // up if any of them refuses. The main window refuses whenever "close to
    // tray" is on -- which meant that "Quit" in the tray menu only hid the
    // window, and the process stayed alive forever. The close handler consults
    // this to tell a real quit from a window close.
    bool quitting() const { return m_quitting; }

    // Called once from QML with the main window, to hang the tray icon off it.
    Q_INVOKABLE void attachWindow(QObject *window);
    Q_INVOKABLE void quitApplication();

    // --- convolution ------------------------------------------------------
    bool convolutionEnabled() const { return m_convolutionEnabled; }
    void setConvolutionEnabled(bool on);
    QString convolutionFile() const { return m_convolution.path; }
    QString convolutionName() const { return m_convolution.name; }
    int convolutionRate() const { return m_convolution.sampleRate; }
    int deviceRate() const { return m_deviceRate; }
    // APO requires the impulse response to be at the device's sample rate.
    // A mismatch is silently wrong rather than an error, so it is surfaced.
    bool rateMismatch() const;
    int impulseCount() const { return m_impulses.size(); }

    Q_INVOKABLE bool loadImpulses();
    // [{ index, name, category, rate, channels, ms, ok }]
    Q_INVOKABLE QVariantList searchImpulses(const QString &needle, int limit = 200);
    Q_INVOKABLE bool selectImpulse(int index);
    Q_INVOKABLE void clearConvolution();

    // --- spectrum ---------------------------------------------------------
    bool spectrumEnabled() const { return m_spectrumEnabled; }
    void setSpectrumEnabled(bool on);
    QVector<float> spectrum() const { return m_spectrum; }
    // False once the capture has gone quiet, so the UI can say so.
    bool spectrumLive() const { return m_spectrumLive; }

    // --- metering ---------------------------------------------------------
    double peakLevel() const { return m_peakLevel; }
    bool meterAvailable() const { return m_peakLevel >= 0.0; }
    bool metering() const { return m_metering; }
    void setMetering(bool on);

    // --- per-device profiles ---------------------------------------------
    bool perDevice() const { return m_perDevice; }
    void setPerDevice(bool on);

    // --- hotkeys ----------------------------------------------------------
    // [{ id, label, keys }] for the binding table.
    Q_INVOKABLE QVariantList hotkeyActions() const;
    // Takes the QML KeyEvent's modifiers and nativeScanCode directly.
    Q_INVOKABLE bool setHotkey(const QString &actionId, int qtModifiers, int nativeScanCode);
    Q_INVOKABLE void clearHotkey(const QString &actionId);

    // --- AutoEQ -----------------------------------------------------------
    bool autoEqLoaded() const { return m_autoEq.loaded(); }
    int autoEqCount() const { return m_autoEq.entries().size(); }
    Q_INVOKABLE bool loadAutoEq();
    // [{ index, label, device, measurer, target, source, hasFixed }]
    Q_INVOKABLE QVariantList searchAutoEq(const QString &needle, int limit = 120);
    Q_INVOKABLE bool importAutoEq(int entryIndex, bool fixedBand);

signals:
    void apoChanged();
    void devicesChanged();
    void currentDeviceChanged();
    void preampChanged();
    void eqEnabledChanged();
    void engagedChanged();
    void generatedTextChanged();
    void lastErrorChanged();
    void lastMessageChanged();
    void currentPresetChanged();
    void dirtyChanged();
    void trayActiveChanged();
    void autostartChanged();
    void closeToTrayChanged();
    void peakLevelChanged();
    void meteringChanged();
    void spectrumChanged();
    void spectrumEnabledChanged();
    void convolutionChanged();
    void impulsesChanged();
    void perDeviceChanged();
    void autoEqChanged();
    void hotkeysChanged();
    void apoStateChanged();
    void apoStatusChanged();
    void quittingChanged();

    // Asked for by a hotkey; the window is QML's business, not the controller's.
    void toggleWindowRequested();

    // Raised from the tray icon; QML decides what to show.
    void trayActivated();
    void trayMenuRequested(int globalX, int globalY);

private:
    void scheduleWrite();
    void writeNow();
    void refreshEngaged();
    void setError(const QString &err);
    void setMessage(const QString &msg);
    void setCurrentPreset(const QString &name);
    void markDirty(bool dirty);

    void saveSession();
    void restoreSession();
    static QString sessionPath();

    Preset currentAsPreset() const;

    static constexpr const char *kIncludeFile = "dreamdsp.txt";

    ApoInstall m_apo;

    // The endpoint an APO action applies to: the selected device, or the
    // system default when the "all devices" entry is selected.
    QString apoTargetDevice() const;

    CompressorModel m_compressor;
    ReverbModel m_reverb;
    EffectsModel m_effects;
    ParamPublisher m_publisher;

    ApoState m_apoState;
    ApoSlot m_apoSlot;
    bool m_apoBusy = false;
    bool m_apoRestartPending = false;
    EqBandModel m_bands;
    PresetStore m_presets;
    QVector<AudioDevice> m_devices;
    QStringList m_deviceNames;
    int m_currentDevice = 0;      // 0 == all devices
    QString m_pendingDeviceId;    // restored from settings before devices are enumerated
    double m_preamp = 0.0;
    bool m_eqEnabled = true;
    bool m_engaged = false;
    bool m_dirty = false;
    bool m_restoring = false;
    QString m_currentPreset;
    QString m_lastError;
    QString m_lastMessage;
    QTimer m_writeTimer;

    void refreshTrayIcon();
    void onHotkey(const QString &actionId);
    void pollMeter();
    void nudgeAllGains(double delta);

    QString profileKeyForDevice(int index) const;
    void stashCurrentProfile();
    void applyProfile(const QString &key);
    void loadProfiles();
    void saveProfiles() const;
    static QString profilesDirectory();

    TrayIcon m_tray;
    HotkeyManager m_hotkeys;
    PeakMeter m_meter;
    QTimer m_meterTimer;
    AutoEqDatabase m_autoEq;
    LoopbackCapture m_capture;
    QTimer m_spectrumIdleTimer;

    QVector<float> m_spectrum;
    bool m_spectrumEnabled = false;
    bool m_spectrumLive = false;

    QVector<ImpulseResponse> m_impulses;
    ImpulseResponse m_convolution;
    bool m_convolutionEnabled = false;
    int m_deviceRate = 0;

    QHash<QString, Preset> m_profiles;   // "" == all devices
    QString m_profileKey;                // the one currently being edited
    bool m_perDevice = false;

    double m_peakLevel = -1.0;
    bool m_metering = false;
    bool m_closeToTray = true;
    bool m_startHidden = false;
    bool m_quitting = false;
};

} // namespace dreamdsp
