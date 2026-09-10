#include "app/AppController.h"

#include "ImpulseAnalysis.h"
#include "WavFile.h"

#include "core/ApoConfig.h"
#include "core/GraphicCurve.h"
#include "platform/Autostart.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QWindow>

#include <algorithm>

namespace dreamdsp {

namespace {
constexpr int kWriteDebounceMs = 150;
const char *kAllDevices = "所有设备";

// APO parses numbers with a C locale and rewrites commas to periods; format
// everything explicitly rather than trusting QString::number's locale rules.
QString num(double v, int decimals)
{
    return QString::asprintf("%.*f", decimals, v);
}
} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    std::memcpy(m_order, dsp::kDefaultOrder, sizeof m_order);

    m_apo = locateApo();
    m_presets.setImportDirectory(m_apo.configPath);

    // APO coalesces change notifications in a 10 ms window; writing on every
    // slider frame would both thrash the file and risk it reading a torn write.
    m_writeTimer.setSingleShot(true);
    m_writeTimer.setInterval(kWriteDebounceMs);
    connect(&m_writeTimer, &QTimer::timeout, this, &AppController::writeNow);

    connect(&m_bands, &EqBandModel::bandsChanged, this, [this] {
        emit generatedTextChanged();
        markDirty(true);
        scheduleWrite();
    });

    m_startHidden = QCoreApplication::arguments().contains(QStringLiteral("--tray"));

    connect(&m_tray, &TrayIcon::activated, this, &AppController::trayActivated);
    connect(&m_tray, &TrayIcon::contextRequested, this, [this](const QPoint &p) {
        emit trayMenuRequested(p.x(), p.y());
    });

    connect(&m_hotkeys, &HotkeyManager::triggered, this, &AppController::onHotkey);

    // Installing an update replaces files this process has open, so the
    // updater asks rather than doing it: quitApplication is the same door the
    // tray menu uses, which flushes settings on the way out.
    connect(&m_updates, &UpdateChecker::quitRequested,
            this, &AppController::quitApplication);

    // 30 Hz is smooth enough for a level meter and cheap; it only runs while
    // something is actually looking at it.
    m_meterTimer.setInterval(33);
    connect(&m_meterTimer, &QTimer::timeout, this, &AppController::pollMeter);

    // Cross-thread, so this arrives queued on the GUI thread.
    connect(&m_capture, &LoopbackCapture::spectrumReady, this,
            [this](const QVector<float> &bands) {
                m_spectrum = bands;
                m_spectrumLive = true;
                m_spectrumIdleTimer.start();
                emit spectrumChanged();
            });
    connect(&m_capture, &LoopbackCapture::failed, this, [this](const QString &why) {
        setError(QStringLiteral("频谱不可用: %1").arg(why));
        m_spectrumEnabled = false;
        emit spectrumEnabledChanged();
    });

    // WASAPI loopback simply stops delivering packets when nothing is playing,
    // rather than sending silence -- without this the last spectrum would hang
    // on screen forever.
    m_spectrumIdleTimer.setSingleShot(true);
    m_spectrumIdleTimer.setInterval(400);
    connect(&m_spectrumIdleTimer, &QTimer::timeout, this, [this] {
        m_spectrumLive = false;
        m_spectrum.clear();
        emit spectrumChanged();
    });

    loadProfiles();

    // Every effect change reaches the copy inside audiodg, whether it came from
    // a slider, a preset, or restoring the last session. The publisher
    // coalesces, so connecting the fine-grained signals directly is fine.
    m_publisher.setSources(&m_compressor, &m_reverb, &m_effects, &m_bands, &m_output);
    connect(&m_output, &OutputModel::changed, this, [this] {
        markDirty(true);
        m_publisher.schedule();
    });

    // Only while the correction is switched on: this is a COM call, and one
    // every half second for a feature nobody is using is a cost with no buyer.
    m_volumeTimer.setInterval(500);
    connect(&m_volumeTimer, &QTimer::timeout, this, &AppController::pollVolume);
    connect(&m_output, &OutputModel::changed, this, [this] {
        if (m_output.loudnessOn() && !m_volumeTimer.isActive()) {
            pollVolume();
            m_volumeTimer.start();
        } else if (!m_output.loudnessOn() && m_volumeTimer.isActive()) {
            m_volumeTimer.stop();
        }
    });
    connect(&m_compressor, &CompressorModel::paramsChanged,
            &m_publisher, &ParamPublisher::schedule);
    connect(&m_reverb, &ReverbModel::paramsChanged,
            &m_publisher, &ParamPublisher::schedule);
    connect(&m_effects, &EffectsModel::changed,
            &m_publisher, &ParamPublisher::schedule);
    connect(&m_publisher, &ParamPublisher::statusChanged, this, [this] {
        // The channel count comes from the APO's own report rather than from a
        // device query: it is the number the processing object is actually
        // handed, which is what the routing and delay controls have to match.
        const dsp::StatusBlock &s = m_publisher.status();
        for (uint32_t i = 0; i < s.instanceCount; ++i) {
            if (s.inst[i].flags & dsp::kSfStreaming) {
                m_output.setChannels(int(s.inst[i].channels));
                break;
            }
        }
        emit apoStatusChanged();
    });

    restoreSession();
    refreshDevices();
    // One reading at start-up so the control shows a number rather than "cannot
    // read"; the repeating poll only runs while the correction is switched on.
    pollVolume();
    refreshEngaged();
    refreshApoState();

    // Continue the generation counter from the file rather than restarting at
    // 1: an APO that has been streaming all along must never see the sequence
    // go backwards. Then publish once, so a fresh audiodg has something to read
    // even if the user never touches a control.
    m_publisher.load();
    // params.bin is what the APO is actually running, so it wins over the
    // QSettings copy for the switch's position. The file name still comes from
    // QSettings -- the blob is content-addressed and does not carry one.
    if (m_publisher.convolution().irGeneration != 0u) {
        m_convolutionEnabled = m_publisher.convolutionEnabled();
    } else if (!m_convolution.path.isEmpty() && QFileInfo::exists(m_convolution.path)) {
        // Nothing published at all, but the session remembers a file. A
        // parameter file that was lost or refused -- a format change being the
        // usual reason -- would otherwise leave the interface naming an impulse
        // response that is not loaded, which is precisely the state a user
        // cannot tell apart from a working one. Republish it and keep the
        // switch position restoreSession() read out of QSettings.
        QString err;
        if (!m_publisher.publishImpulse(m_convolution.path, &err))
            setError(QStringLiteral("脉冲响应重新载入失败:%1").arg(err));
        else
            m_publisher.setConvolutionEnabled(m_convolutionEnabled);
    }
    // The curve and the order come back out of the same file the effects do.
    m_graphic = m_publisher.graphic();
    m_graphicEnabled = m_publisher.graphicEnabled();
    std::memcpy(m_order, m_publisher.order(), sizeof m_order);
    emit graphicChanged();
    emit chainChanged();

    // Settles who applies the equalizer before the first block goes out, so
    // that a machine where both are present never gets one buffer of doubled
    // gain during start-up.
    syncEqOwnership();
    flushParams();
    refreshImpulseCurve();

    // Make sure our include file exists from the start; it is inert until the
    // user engages it, so this cannot change what they hear.
    if (m_apo.found)
        writeNow();
    markDirty(false);

    // Checked shortly after start-up rather than during it: the window should
    // appear without waiting on the network, and a failed check must never be
    // able to delay or break launching.
    if (m_updates.automatic())
        QTimer::singleShot(4000, &m_updates, &UpdateChecker::checkNow);
}

AppController::~AppController()
{
    // Losing the last slider move because the process exited would be a
    // genuinely annoying bug.
    if (m_writeTimer.isActive()) {
        m_writeTimer.stop();
        writeNow();
    }
    // The same reasoning for the parameter channel: a slider moved in the last
    // 30 ms would otherwise be lost, and it is the session store as well.
    flushParams();
    saveSession();
}

// ---------------------------------------------------------------- properties

void AppController::setCurrentDevice(int index)
{
    if (index < 0 || index >= m_deviceNames.size() || index == m_currentDevice)
        return;

    // In per-device mode the combo selects which curve you are editing, so the
    // one you are leaving has to be put away first.
    if (m_perDevice)
        stashCurrentProfile();

    m_currentDevice = index;

    if (m_perDevice)
        applyProfile(profileKeyForDevice(index));

    if (m_metering)
        m_meter.attach(profileKeyForDevice(index));
    if (m_spectrumEnabled)
        m_capture.startCapture(profileKeyForDevice(index));

    m_deviceRate = dreamdsp::deviceSampleRate(profileKeyForDevice(index));
    emit convolutionChanged();

    // The APO attaches per endpoint, so which one is selected changes what the
    // settings page has to say about it.
    refreshApoState();

    emit currentDeviceChanged();
    emit generatedTextChanged();
    scheduleWrite();
}

void AppController::setPreamp(double db)
{
    db = std::clamp(db, -30.0, 30.0);
    if (qFuzzyCompare(m_preamp + 1.0, db + 1.0))
        return;
    m_preamp = db;
    emit preampChanged();
    emit generatedTextChanged();
    markDirty(true);
    scheduleWrite();
}

void AppController::setMasterEnabled(bool on)
{
    if (m_masterEnabled == on)
        return;
    m_masterEnabled = on;
    emit masterEnabledChanged();
    emit eqEnabledChanged();      // the engine label reads both
    refreshTrayIcon();
    scheduleWrite();
}

void AppController::setEqEnabled(bool on)
{
    if (m_eqEnabled == on)
        return;
    m_eqEnabled = on;
    emit eqEnabledChanged();
    emit generatedTextChanged();
    refreshTrayIcon();
    scheduleWrite();
}

void AppController::setEngaged(bool on)
{
    if (!m_apo.found) {
        setError(QStringLiteral("未检测到 Equalizer APO"));
        return;
    }
    if (m_engaged == on)
        return;

    const QString configTxt = configFilePath(m_apo, QStringLiteral("config.txt"));
    QStringList lines;
    QString err;
    if (!ApoConfig::readLines(configTxt, &lines, &err)) {
        setError(QStringLiteral("读取 config.txt 失败: %1").arg(err));
        return;
    }

    const QString include = QString::fromLatin1(kIncludeFile);
    lines = on ? ApoConfig::withInclude(lines, include)
               : ApoConfig::withoutInclude(lines, include);

    if (!ApoConfig::writeLines(configTxt, lines, &err)) {
        setError(QStringLiteral("写入 config.txt 失败: %1").arg(err));
        return;
    }

    setError({});
    setMessage(on ? QStringLiteral("已接管 —— config.txt 现在包含 dreamdsp.txt")
                  : QStringLiteral("已停止接管 —— 已从 config.txt 移除"));
    m_engaged = on;
    emit engagedChanged();
    emit eqEngineChanged();
    refreshTrayIcon();
    // Hands the equalizer over. Engaging APO switches ours off and disengaging
    // switches it back on, so the curve is applied exactly once either way.
    scheduleWrite();
}

// ------------------------------------------------------------------- output

namespace {

// One APO block: an optional Device: guard, a Preamp, the filters, and an
// optional convolution. Order matters -- the impulse response is applied after
// the equalizer, which is what you want when the IR models a speaker or room.
void appendBlock(QString &out, const Preset &p, const QString &deviceName,
                 const QString &convolutionPath = QString())
{
    if (!deviceName.isEmpty())
        out += QStringLiteral("Device: %1\r\n").arg(deviceName);

    out += QStringLiteral("Preamp: %1 dB\r\n").arg(num(p.preamp, 1));

    int n = 1;
    for (const PresetBand &b : p.bands) {
        if (!b.enabled)
            continue;
        // Skip peaking bands sitting at exactly 0 dB: they are audibly inert
        // and only make the generated file harder to read.
        if (b.type == FilterType::PK && qFuzzyIsNull(b.gainDb))
            continue;

        QString line = QStringLiteral("Filter %1: ON %2 Fc %3 Hz")
                           .arg(n++)
                           .arg(QString::fromLatin1(apoToken(b.type)))
                           .arg(num(b.frequency, 0));
        if (hasGain(b.type))
            line += QStringLiteral(" Gain %1 dB").arg(num(b.gainDb, 1));
        if (hasQ(b.type))
            line += QStringLiteral(" Q %1").arg(num(b.q, 2));
        out += line + QStringLiteral("\r\n");
    }

    if (n == 1 && convolutionPath.isEmpty())
        out += QStringLiteral("# (all bands flat)\r\n");

    if (!convolutionPath.isEmpty())
        out += QStringLiteral("Convolution: %1\r\n").arg(convolutionPath);
}

} // namespace

QString AppController::eqEngine() const
{
    if (!m_masterEnabled)
        return QStringLiteral("总开关已关");
    if (!m_eqEnabled)
        return QStringLiteral("已旁通");
    if (m_apo.found && m_engaged)
        return QStringLiteral("Equalizer APO");
    if (nativeProcessing())
        return QStringLiteral("DreamDSP");
    return QStringLiteral("未接入音频");
}

QString AppController::generatedText() const
{
    QString out;
    out += QStringLiteral("# Generated by DreamDSP %1 -- edits will be overwritten.\r\n")
               .arg(QStringLiteral(DREAMDSP_VERSION));

    // Kept current even while nothing includes it, so that engaging Equalizer
    // APO is a one-line change rather than a regeneration -- but say plainly
    // that it is not the file doing the work, because a stale-looking config
    // that is nevertheless being obeyed is the worst of the two states.
    if (nativeProcessing())
        out += QStringLiteral("# DreamDSP is processing this endpoint itself; "
                              "nothing includes this file.\r\n");

    if (!m_eqEnabled) {
        out += QStringLiteral("# (equalizer disabled)\r\n");
        return out;
    }

    // Nothing goes into Equalizer APO's config when DreamDSP is convolving it
    // itself -- doing both would apply the impulse response twice.
    const QString conv = (m_convolutionEnabled && !m_convolution.path.isEmpty()
                          && !nativeConvolution())
                             ? m_convolution.path
                             : QString();

    if (!m_perDevice) {
        // One curve. A specific device selection still guards it, so the
        // equalizer only touches the endpoint you picked.
        const QString deviceName = (m_currentDevice > 0 && m_currentDevice - 1 < m_devices.size())
                                       ? m_devices.at(m_currentDevice - 1).name
                                       : QString();
        appendBlock(out, currentAsPreset(), deviceName, conv);
        return out;
    }

    // Per-device: every stored profile is emitted, each behind its own Device:
    // guard, so all endpoints are handled at once instead of only the selected
    // one. The profile being edited right now comes from the live state.
    QHash<QString, Preset> all = m_profiles;
    all.insert(m_profileKey, currentAsPreset());

    if (const auto it = all.constFind(QString()); it != all.constEnd() && it->isValid()) {
        out += QStringLiteral("# --- all devices ---\r\n");
        appendBlock(out, *it, QString(), conv);
    }

    for (const AudioDevice &dev : m_devices) {
        const auto it = all.constFind(dev.id);
        if (it == all.constEnd() || !it->isValid())
            continue;
        out += QStringLiteral("\r\n# --- %1 ---\r\n").arg(dev.name);
        appendBlock(out, *it, dev.name, conv);
    }
    return out;
}

// ------------------------------------------------------------------ actions

void AppController::refreshDevices()
{
    QString err;
    m_devices = enumerateRenderDevices(&err);
    if (!err.isEmpty())
        setError(QStringLiteral("枚举音频设备失败: %1").arg(err));

    m_deviceNames.clear();
    m_deviceNames << QString::fromUtf8(kAllDevices);
    for (const AudioDevice &d : std::as_const(m_devices))
        m_deviceNames << (d.active ? d.name : QStringLiteral("%1 (未连接)").arg(d.name));

    // Devices are remembered by endpoint ID, not by list position -- the order
    // changes whenever something is plugged in.
    if (!m_pendingDeviceId.isEmpty()) {
        for (int i = 0; i < m_devices.size(); ++i) {
            if (m_devices.at(i).id == m_pendingDeviceId) {
                m_currentDevice = i + 1;
                break;
            }
        }
        m_pendingDeviceId.clear();
    }
    if (m_currentDevice >= m_deviceNames.size())
        m_currentDevice = 0;

    // Needed for the convolution sample-rate check.
    m_deviceRate = dreamdsp::deviceSampleRate(profileKeyForDevice(m_currentDevice));
    pollVolume();

    emit devicesChanged();
    emit currentDeviceChanged();
    emit convolutionChanged();
}

void AppController::resetAll()
{
    m_bands.zeroGains();
    setPreamp(0.0);

    // The effects too. They used to be left alone because they lived in QML and
    // this object could not reach them; now that it owns them, "reset
    // everything" that quietly skipped half the application would be a lie --
    // and it is the way back for anyone whose settings were damaged.
    m_compressor.restore(dsp::Compressor::Params{}, false);
    m_reverb.restore(dsp::Reverb::Params{}, false);
    {
        dsp::ParamBlock defaults;
        std::memset(&defaults, 0, sizeof defaults);
        defaults.tube = dsp::TubeStage::Params{};
        defaults.bass = dsp::VirtualBass::Params{};
        defaults.exciter = dsp::Exciter::Params{};
        defaults.width = dsp::StereoWidener::Params{};
        defaults.crossfeed = dsp::Crossfeed::Params{};
        defaults.multiband = dsp::MultibandCompressor::Params{};
        defaults.enableMask = 0;
        m_effects.restore(defaults);
    }

    // The output stage as well: routing back to straight-through, no delays,
    // no loudness correction. "Reset everything" that left a channel swap in
    // place would be the most confusing possible outcome.
    {
        dsp::ParamBlock defaults = dsp::transparentBlock();
        defaults.loudness.amount = 1.0f;
        m_output.restore(defaults);
    }

    clearGraphicEq();
    resetChainOrder();

    setMessage(QStringLiteral("已全部归零(均衡器、效果与输出)"));
}

// ------------------------------------------------------------- full presets

QString AppController::fullPresetPath(const QString &name) const
{
    return QDir(PresetStore::userDirectory())
        .filePath(name + QStringLiteral(".dreamdsp"));
}

bool AppController::saveFullPreset(const QString &name)
{
    if (name.trimmed().isEmpty()) {
        setError(QStringLiteral("预设名不能为空"));
        return false;
    }

    DreamPreset p;
    p.name = name;
    p.eq = currentAsPreset();
    p.eqEnabled = m_eqEnabled;
    // Straight from the publisher, so a preset holds byte-for-byte what the
    // DSP is running rather than a second reading of the same models.
    //
    // The equalizer is deliberately left out of the block: p.eq above already
    // holds it, as a readable band list rather than 520 bytes of base64, and
    // two copies of the same curve in one file is one copy too many.
    p.params = blockFromModels(&m_compressor, &m_reverb, &m_effects,
                               m_publisher.convolution(), m_convolutionEnabled,
                               nullptr, 0.0, false, &m_output);
    p.convolutionFile = m_convolution.path;
    p.convolutionName = m_convolution.name;
    p.autoEqSource = m_autoEqSource;

    QDir().mkpath(PresetStore::userDirectory());
    QString err;
    if (!saveDreamPreset(fullPresetPath(name), p, &err)) {
        setError(QStringLiteral("保存失败:%1").arg(err));
        return false;
    }

    setError({});
    setMessage(QStringLiteral("已保存完整预设「%1」").arg(name));
    emit fullPresetsChanged();
    return true;
}

bool AppController::loadFullPreset(const QString &path)
{
    DreamPreset p;
    QString err;
    if (!loadDreamPreset(path, &p, &err)) {
        setError(QStringLiteral("读取失败:%1").arg(err));
        return false;
    }

    m_restoring = true;
    m_bands.setBands(p.eq.bands);
    setPreamp(p.eq.preamp);
    m_restoring = false;
    setEqEnabled(p.eqEnabled);

    m_compressor.restore(p.params.comp, (p.params.enableMask & dsp::kEnComp) != 0);
    m_reverb.restore(p.params.reverb, (p.params.enableMask & dsp::kEnReverb) != 0);
    m_effects.restore(p.params);
    m_output.restore(p.params);
    m_autoEqSource = p.autoEqSource;

    m_publisher.setConvolutionMix(p.params.convolution.mix);
    m_publisher.setConvolutionTrimDb(p.params.convolution.trimDb);

    // The impulse response is referred to by path. A preset moved between
    // machines can name a file that is not here, which is worth saying rather
    // than silently dropping.
    const bool wantConvolution = (p.params.enableMask & dsp::kEnConvolution) != 0;
    if (!p.convolutionFile.isEmpty() && QFileInfo::exists(p.convolutionFile)) {
        applyImpulseFile(p.convolutionFile);
        m_publisher.setConvolutionEnabled(wantConvolution);
        setConvolutionEnabled(wantConvolution);
    } else if (!p.convolutionFile.isEmpty()) {
        setError(QStringLiteral("预设里的脉冲响应不在这台机器上:%1").arg(p.convolutionFile));
    }

    setCurrentPreset(p.name);
    markDirty(false);
    flushParams();
    emit generatedTextChanged();
    scheduleWrite();

    if (lastError().isEmpty())
        setMessage(QStringLiteral("已载入完整预设「%1」").arg(p.name));
    return true;
}

QVariantList AppController::fullPresets() const
{
    QVariantList out;
    QDir dir(PresetStore::userDirectory());
    const QStringList files = dir.entryList({ QStringLiteral("*.dreamdsp") },
                                            QDir::Files, QDir::Name);
    for (const QString &f : files) {
        const QString path = dir.filePath(f);
        out.append(QVariantMap{
            { QStringLiteral("name"), QFileInfo(f).completeBaseName() },
            { QStringLiteral("path"), path },
        });
    }
    return out;
}

bool AppController::deleteFullPreset(const QString &path)
{
    if (!QFile::remove(path)) {
        setError(QStringLiteral("无法删除 %1").arg(path));
        return false;
    }
    setMessage(QStringLiteral("已删除预设"));
    emit fullPresetsChanged();
    return true;
}

void AppController::openConfigFolder()
{
    if (!m_apo.configPath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_apo.configPath));
}

void AppController::openPresetFolder()
{
    const QString dir = PresetStore::userDirectory();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void AppController::flushNow()
{
    m_writeTimer.stop();
    writeNow();
    saveSession();
}

// ----------------------------------------------------------------------- apo
//
// Getting DreamDSP's own processing object into the Windows mixing chain. This
// is what makes the application affect audio directly rather than by writing a
// configuration file for Equalizer APO to execute.

// What the elevated helper left behind. It runs hidden, so this is the only
// place its reason for failing survives; without it the user gets an exit code.
QString AppController::installLogTail(const QString &fallback) const
{
    QFile log(QStringLiteral("C:/ProgramData/DreamDSP/install.log"));
    if (!log.open(QIODevice::ReadOnly | QIODevice::Text))
        return fallback;
    const QStringList lines = QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'),
                                                                    Qt::SkipEmptyParts);
    if (lines.size() < 2)
        return fallback;
    const QString last = lines.last().trimmed();
    return last.isEmpty() || last == QStringLiteral("ok") ? fallback : last;
}

bool AppController::apoLive() const
{
    if (!m_publisher.statusFresh())
        return false;
    for (uint32_t i = 0; i < m_publisher.status().instanceCount; ++i) {
        if (m_publisher.status().inst[i].flags & dsp::kSfStreaming)
            return true;
    }
    return false;
}

bool AppController::apoInSync() const
{
    return m_publisher.statusFresh()
           && m_publisher.status().loadedGeneration == m_publisher.generation();
}

QString AppController::apoStatusText() const
{
    if (!m_apoState.installed())
        return QStringLiteral("音频组件未安装");

    // Checked ahead of everything else because it explains an otherwise
    // impossible-looking state: registered, attached, and completely inert.
    if (m_apoSlot.sysFxDisabled) {
        return QStringLiteral("这台设备关闭了「所有声音增强」—— Windows 不会加载任何音效对象,"
                              "包括 DreamDSP。点上面的「接入当前输出设备」可以打开它");
    }
    if (!m_apoSlot.isOurs)
        return QStringLiteral("未接入这台设备");
    if (!m_publisher.statusFresh())
        return QStringLiteral("尚未收到运行反馈 —— 播放一点声音看看");

    const dsp::StatusBlock &s = m_publisher.status();
    if (s.rejectCount > 0 && s.loadCount == 0)
        return QStringLiteral("参数文件被拒绝了 %1 次 —— 版本可能不匹配").arg(s.rejectCount);
    if (s.instanceCount == 0)
        return QStringLiteral("已加载,但当前没有音频流经过");

    uint32_t mask = 0;
    for (uint32_t i = 0; i < s.instanceCount; ++i)
        mask |= s.inst[i].appliedMask;

    if (mask == 0)
        return QStringLiteral("正在直通(没有启用任何效果)");
    if (!apoInSync())
        return QStringLiteral("正在生效,但参数还在同步(第 %1 代 → 第 %2 代)")
            .arg(s.loadedGeneration).arg(m_publisher.generation());
    return QStringLiteral("正在处理 %1 路音频流").arg(s.instanceCount);
}

QString AppController::apoTargetDevice() const
{
    // An APO attaches to one endpoint, so the "all devices" entry has no
    // meaning here; fall back to whichever endpoint Windows is currently
    // playing through.
    const QString key = profileKeyForDevice(m_currentDevice);
    if (!key.isEmpty())
        return key;
    for (const AudioDevice &d : m_devices) {
        if (d.isDefault)
            return d.id;
    }
    return m_devices.isEmpty() ? QString() : m_devices.first().id;
}

void AppController::refreshApoState()
{
    const bool wasNative = nativeProcessing();

    m_apoState = apoState();
    m_apoSlot = apoSlotOf(apoTargetDevice());
    emit apoStateChanged();
    emit eqEngineChanged();
    // The running-state line reads m_apoState and m_apoSlot as well as the
    // APO's own report, so it has to be told when those change. Binding it to
    // the report alone left it frozen at whatever it happened to say when the
    // page was first built.
    emit apoStatusChanged();

    // Attaching to an endpoint is the moment the equalizer moves house. Doing
    // it here rather than in setApoAttached covers every route to the same
    // state -- a device switch, an install, the elevated helper coming back.
    if (nativeProcessing() != wasNative) {
        syncEqOwnership();
        scheduleWrite();
    }
}

void AppController::installApo()
{
    if (m_apoBusy)
        return;
    m_apoBusy = true;
    emit apoStateChanged();

    // Staging the DLL fails while audiodg still has the previous copy mapped,
    // so the install and the service restart go in one elevated pass -- one
    // consent prompt, and the restart releases the file for the next one.
    const QString err = runElevated({ QStringLiteral("--apo-install"),
                                      QStringLiteral("--apo-restart-audio") });
    m_apoBusy = false;
    refreshApoState();

    if (!err.isEmpty()) {
        if (err == QStringLiteral("已取消"))
            setMessage(QStringLiteral("安装已取消"));
        else
            setError(QStringLiteral("安装失败:%1").arg(installLogTail(err)));
        return;
    }

    // Reporting success on the strength of an exit code is not enough: the
    // interesting failure is the one where every step returns success and the
    // old DLL is still sitting there, which is exactly what happened the first
    // time. Check the thing that actually matters.
    if (!m_apoState.upToDate) {
        setError(QStringLiteral("安装过程未报错,但暂存的组件仍是旧版本 —— %1")
                     .arg(installLogTail(QStringLiteral("原因不明"))));
        return;
    }

    m_apoRestartPending = false;
    setMessage(QStringLiteral("DreamDSP 音频组件已安装并更新到当前版本"));
}

void AppController::uninstallApo()
{
    if (m_apoBusy)
        return;

    // Detach from every endpoint first: those writes need no elevation, and
    // leaving our CLSID in an endpoint's slot after unregistering it would give
    // that device an effect chain pointing at nothing.
    for (const AudioDevice &d : m_devices)
        detachApo(d.id);

    m_apoBusy = true;
    emit apoStateChanged();

    const QString err = runElevated({ QStringLiteral("--apo-uninstall"),
                                      QStringLiteral("--apo-restart-audio") });
    m_apoBusy = false;

    if (err.isEmpty()) {
        m_apoRestartPending = false;
        setMessage(QStringLiteral("DreamDSP 音频组件已卸载,原有效果已还原"));
    } else if (err == QStringLiteral("已取消")) {
        setMessage(QStringLiteral("卸载已取消"));
    } else {
        setError(QStringLiteral("卸载失败:%1").arg(err));
    }
    refreshApoState();
}

void AppController::setApoAttached(bool on)
{
    const QString device = apoTargetDevice();
    if (device.isEmpty()) {
        setError(QStringLiteral("没有可用的输出设备"));
        return;
    }

    // No elevation: BUILTIN\Users holds SetValue on an endpoint's FxProperties.
    const QString err = on ? attachApo(device) : detachApo(device);
    if (!err.isEmpty()) {
        setError(err);
        refreshApoState();
        return;
    }

    m_apoRestartPending = true;
    setMessage(on ? QStringLiteral("已挂载到当前设备 · 重启音频服务后生效")
                  : QStringLiteral("已从当前设备移除 · 重启音频服务后生效"));
    refreshApoState();
}

void AppController::restartAudio()
{
    if (m_apoBusy)
        return;
    m_apoBusy = true;
    emit apoStateChanged();

    const QString err = runElevated({ QStringLiteral("--apo-restart-audio") });
    m_apoBusy = false;

    if (err.isEmpty()) {
        m_apoRestartPending = false;
        setMessage(QStringLiteral("音频服务已重启,更改已生效"));
    } else if (err == QStringLiteral("已取消")) {
        setMessage(QStringLiteral("已取消"));
    } else {
        setError(err);
    }
    refreshApoState();
}

// ---------------------------------------------------------------------- tray

bool AppController::trayActive() const
{
    return m_tray.installed();
}

void AppController::attachWindow(QObject *window)
{
    auto *win = qobject_cast<QWindow *>(window);
    if (!win || m_tray.installed())
        return;

    // winId() forces window creation, so the HWND is valid from here on.
    if (m_tray.install(win->winId())) {
        refreshTrayIcon();
        emit trayActiveChanged();
    }
}

void AppController::refreshTrayIcon()
{
    if (!m_tray.installed())
        return;

    // Drawn rather than shipped as an .ico so it can reflect state: the bars go
    // grey when the equalizer is switched off, which is the one thing worth
    // knowing at a glance from the notification area.
    const int side = std::max(16, GetSystemMetrics(SM_CXSMICON));
    QImage img(side, side, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor on(0x3d, 0x7e, 0xff);
    const QColor off(0x9a, 0x9a, 0x9a);
    p.setBrush((m_masterEnabled && m_eqEnabled) ? on : off);
    p.setPen(Qt::NoPen);

    // Three bars at different heights -- a tiny equalizer.
    const qreal barW = side / 6.0;
    const qreal gap = side / 8.0;
    const qreal totalW = barW * 3 + gap * 2;
    qreal x = (side - totalW) / 2.0;
    const qreal heights[3] = { 0.55, 0.85, 0.40 };
    for (const qreal hf : heights) {
        const qreal h = side * hf;
        p.drawRoundedRect(QRectF(x, side - h - side * 0.1, barW, h), barW / 2.0, barW / 2.0);
        x += barW + gap;
    }
    p.end();

    m_tray.setIcon(img);
    m_tray.setToolTip((m_masterEnabled && m_eqEnabled)
                          ? QStringLiteral("DreamDSP — 均衡器已启用%1")
                                .arg(m_engaged ? QString() : QStringLiteral(" (未接管)"))
                          : QStringLiteral("DreamDSP — 均衡器已关闭"));
}

void AppController::quitApplication()
{
    flushNow();
    m_tray.uninstall();

    // Must be set before quit(): quit() asks the main window to close, and the
    // window's close handler will veto that if it still thinks this is an
    // ordinary close and "minimise to tray" is on. Without this the tray menu's
    // Quit entry simply hid the window and the process never exited.
    m_quitting = true;
    emit quittingChanged();

    QCoreApplication::quit();
}

// -------------------------------------------------------------- convolution

bool AppController::nativeConvolution() const
{
    // Otherwise the work has to be handed to Equalizer APO, which is the
    // fallback rather than the default because its Convolution: command
    // requires the file to be at exactly the device rate and is silently wrong
    // when it is not.
    return nativeProcessing();
}

bool AppController::rateMismatch() const
{
    // Meaningless once DreamDSP is doing the convolution: the impulse response
    // is converted to whatever rate the endpoint is running at, inside the APO,
    // which is the point of doing it ourselves.
    if (nativeConvolution())
        return false;
    // Only meaningful once both rates are known; an unreadable impulse
    // response (flac/ogg) reports rate 0 and cannot be pre-checked.
    return m_convolution.sampleRate > 0 && m_deviceRate > 0
           && m_convolution.sampleRate != m_deviceRate;
}

bool AppController::loadImpulses()
{
    if (!m_impulses.isEmpty())
        return true;

    m_impulses = scanImpulseResponses(
        defaultImpulseRoots(m_apo.configPath, PresetStore::userDirectory() + QStringLiteral("/..")));

    emit impulsesChanged();
    if (m_impulses.isEmpty()) {
        setError(QStringLiteral("没有找到脉冲响应文件"));
        return false;
    }
    setMessage(QStringLiteral("找到 %1 个脉冲响应").arg(m_impulses.size()));
    return true;
}

QVariantList AppController::searchImpulses(const QString &needle, int limit)
{
    QVariantList out;
    const QString n = needle.trimmed();

    for (int i = 0; i < m_impulses.size() && out.size() < limit; ++i) {
        const ImpulseResponse &ir = m_impulses.at(i);
        if (!n.isEmpty()
            && !ir.name.contains(n, Qt::CaseInsensitive)
            && !ir.category.contains(n, Qt::CaseInsensitive)) {
            continue;
        }
        out.append(QVariantMap{
            { QStringLiteral("index"), i },
            { QStringLiteral("name"), ir.name },
            { QStringLiteral("category"), ir.category },
            { QStringLiteral("rate"), ir.sampleRate },
            { QStringLiteral("channels"), ir.channels },
            { QStringLiteral("ms"), int(ir.durationMs() + 0.5) },
            { QStringLiteral("ok"), ir.readable },
            // Flag the ones that cannot work on the current device.
            { QStringLiteral("mismatch"),
              ir.sampleRate > 0 && m_deviceRate > 0 && ir.sampleRate != m_deviceRate },
        });
    }
    return out;
}

void AppController::setConvolutionMix(double v)
{
    m_publisher.setConvolutionMix(v);
    emit convolutionChanged();
}

void AppController::setConvolutionTrim(double v)
{
    m_publisher.setConvolutionTrimDb(v);
    emit convolutionChanged();
}

// Measures what the selected impulse response does to the spectrum.
//
// Runs on the GUI thread on purpose. A transform of a few tens of thousands of
// points takes single-digit milliseconds, and it happens once per selection --
// pushing it onto a worker would buy nothing and cost a synchronisation
// problem.
void AppController::refreshImpulseCurve()
{
    m_impulseCurve.clear();

    if (!m_convolution.path.isEmpty()) {
        dsp::WavData wav;
        std::string err;
        if (dsp::readWav(m_convolution.path.toStdString(), &wav, &err)
            && wav.frames() > 0 && wav.channelCount() > 0) {
            // Planar, contiguous, which is what the analyser wants.
            std::vector<float> planar(size_t(wav.frames()) * wav.channelCount(), 0.0f);
            for (int c = 0; c < wav.channelCount(); ++c) {
                std::copy(wav.channels[size_t(c)].begin(), wav.channels[size_t(c)].end(),
                          planar.begin() + qsizetype(c) * wav.frames());
            }
            const dsp::ImpulseCurve curve = dsp::impulseMagnitudeResponsePlanar(
                planar.data(), wav.frames(), wav.channelCount(), wav.sampleRate, 256);
            if (curve.valid()) {
                m_impulseCurve.reserve(int(curve.magnitudeDb.size()));
                for (float v : curve.magnitudeDb)
                    m_impulseCurve.append(v);
                m_impulseCurveFloor = curve.suggestedFloorDb;
            }
        }
    }

    emit impulseCurveChanged();
}

bool AppController::applyImpulseFile(const QString &path)
{
    ImpulseResponse ir;
    if (!readWaveHeader(path, &ir)) {
        setError(QStringLiteral("无法读取 %1").arg(path));
        return false;
    }
    m_impulses.append(ir);
    return selectImpulse(int(m_impulses.size()) - 1);
}

bool AppController::selectImpulse(int index)
{
    if (index < 0 || index >= m_impulses.size())
        return false;

    m_convolution = m_impulses.at(index);
    m_convolutionEnabled = true;

    if (nativeConvolution()) {
        // Hand the samples to our own engine. The file goes across at its own
        // rate; the APO converts it to whatever the endpoint is running at.
        // Set first: publishImpulse writes params.bin straight away, and a
        // scheduled enable arriving 30 ms later would miss that write -- and
        // never happen at all if the process exits in between.
        m_publisher.setConvolutionEnabled(true);
        QString err;
        if (!m_publisher.publishImpulse(m_convolution.path, &err)) {
            m_convolutionEnabled = false;
            emit convolutionChanged();
            m_publisher.setConvolutionEnabled(false);
            setError(QStringLiteral("无法使用「%1」:%2").arg(m_convolution.name, err));
            return false;
        }
        setError({});
        setMessage(QStringLiteral("已应用卷积「%1」(%2 Hz,将自动重采样到设备速率)")
                       .arg(m_convolution.name)
                       .arg(m_convolution.sampleRate));
    } else if (rateMismatch()) {
        setError(QStringLiteral("采样率不匹配:该脉冲响应是 %1 Hz,设备是 %2 Hz —— "
                                "Equalizer APO 要求两者一致。装上 DreamDSP 音频组件即可任意采样率")
                     .arg(m_convolution.sampleRate).arg(m_deviceRate));
    } else {
        setError({});
        setMessage(QStringLiteral("已应用卷积「%1」").arg(m_convolution.name));
    }

    refreshImpulseCurve();
    emit convolutionChanged();
    emit generatedTextChanged();
    scheduleWrite();
    return true;
}

void AppController::clearConvolution()
{
    m_convolution = ImpulseResponse{};
    m_convolutionEnabled = false;
    m_publisher.clearImpulse();
    refreshImpulseCurve();
    emit convolutionChanged();
    emit generatedTextChanged();
    scheduleWrite();
    setMessage(QStringLiteral("已移除卷积"));
}

void AppController::setConvolutionEnabled(bool on)
{
    if (m_convolutionEnabled == on)
        return;
    m_convolutionEnabled = on;
    m_publisher.setConvolutionEnabled(on);
    emit convolutionChanged();
    emit generatedTextChanged();
    scheduleWrite();
}

// ----------------------------------------------------------------- spectrum

void AppController::setSpectrumEnabled(bool on)
{
    if (m_spectrumEnabled == on)
        return;
    m_spectrumEnabled = on;
    emit spectrumEnabledChanged();

    if (on) {
        m_capture.startCapture(profileKeyForDevice(m_currentDevice));
    } else {
        m_capture.stopCapture();
        m_spectrumIdleTimer.stop();
        m_spectrum.clear();
        m_spectrumLive = false;
        emit spectrumChanged();
    }
}

// ----------------------------------------------------------------- metering

void AppController::setMetering(bool on)
{
    if (m_metering == on)
        return;
    m_metering = on;
    emit meteringChanged();

    if (on) {
        m_meter.attach(m_currentDevice > 0 && m_currentDevice - 1 < m_devices.size()
                           ? m_devices.at(m_currentDevice - 1).id
                           : QString());
        m_meterTimer.start();
    } else {
        m_meterTimer.stop();
        m_meter.detach();
        m_peakLevel = -1.0;
        emit peakLevelChanged();
    }
}

void AppController::pollVolume()
{
    if (!m_endpointVolume.attach(profileKeyForDevice(m_currentDevice))) {
        m_output.setVolume(0.0, false);
        return;
    }
    const float db = m_endpointVolume.levelDb();
    // 1.0 is the "no reading" sentinel; a real attenuation is never positive.
    m_output.setVolume(db > 0.0f ? 0.0 : double(db), db <= 0.0f);
}

void AppController::pollMeter()
{
    const float v = m_meter.peak();
    const double level = (v < 0.0f) ? -1.0 : static_cast<double>(v);

    // Only signal on a visible change; at 30 Hz an unconditional notify would
    // re-evaluate every binding on the meter for no reason.
    if (std::abs(level - m_peakLevel) < 0.002 && !(level < 0.0) == !(m_peakLevel < 0.0))
        return;
    m_peakLevel = level;
    emit peakLevelChanged();
}

// ------------------------------------------------------------------ hotkeys

QVariantList AppController::hotkeyActions() const
{
    QVariantList out;
    for (const HotkeyManager::ActionInfo &a : HotkeyManager::actions()) {
        const QString id = QString::fromLatin1(a.id);
        out.append(QVariantMap{
            { QStringLiteral("id"), id },
            { QStringLiteral("label"), QString::fromUtf8(a.label) },
            { QStringLiteral("keys"), m_hotkeys.displayText(id) },
        });
    }
    return out;
}

bool AppController::setHotkey(const QString &actionId, int qtModifiers, int nativeScanCode)
{
    quint32 mods = 0, vk = 0;
    if (!HotkeyManager::translate(qtModifiers, static_cast<quint32>(nativeScanCode), &mods, &vk)) {
        setError(QStringLiteral("这个按键组合不能作为全局热键(需要带 Ctrl/Alt/Shift/Win,或使用 F1–F24 / 媒体键)"));
        return false;
    }

    QString err;
    if (!m_hotkeys.bind(actionId, mods, vk, &err)) {
        setError(err);
        emit hotkeysChanged();
        return false;
    }

    setError({});
    setMessage(QStringLiteral("已绑定 %1").arg(HotkeyManager::describe(mods, vk)));
    QSettings s(QStringLiteral("DreamDSP"), QStringLiteral("DreamDSP"));
    m_hotkeys.save(s);
    emit hotkeysChanged();
    return true;
}

void AppController::clearHotkey(const QString &actionId)
{
    m_hotkeys.clear(actionId);
    QSettings s(QStringLiteral("DreamDSP"), QStringLiteral("DreamDSP"));
    m_hotkeys.save(s);
    emit hotkeysChanged();
}

void AppController::nudgeAllGains(double delta)
{
    for (int i = 0; i < m_bands.rowCount(); ++i)
        m_bands.setGain(i, m_bands.gain(i) + delta);
}

void AppController::onHotkey(const QString &id)
{
    if (id == QLatin1String("toggleEq")) {
        setEqEnabled(!m_eqEnabled);
    } else if (id == QLatin1String("toggleEngage")) {
        setEngaged(!m_engaged);
    } else if (id == QLatin1String("gainUp")) {
        nudgeAllGains(1.0);
    } else if (id == QLatin1String("gainDown")) {
        nudgeAllGains(-1.0);
    } else if (id == QLatin1String("preampUp")) {
        setPreamp(m_preamp + 1.0);
    } else if (id == QLatin1String("preampDown")) {
        setPreamp(m_preamp - 1.0);
    } else if (id == QLatin1String("resetAll")) {
        resetAll();
    } else if (id == QLatin1String("nextPreset") || id == QLatin1String("prevPreset")) {
        const int count = m_presets.rowCount();
        if (count == 0)
            return;
        int cur = -1;
        for (int i = 0; i < count; ++i) {
            if (m_presets.data(m_presets.index(i), PresetStore::NameRole).toString() == m_currentPreset) {
                cur = i;
                break;
            }
        }
        const int step = (id == QLatin1String("nextPreset")) ? 1 : -1;
        loadPreset(((cur + step) % count + count) % count);
    } else if (id == QLatin1String("showWindow")) {
        emit toggleWindowRequested();
    }
}

// ------------------------------------------------------------ per-device

QString AppController::profilesDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("devices"));
}

QString AppController::profileKeyForDevice(int index) const
{
    if (index <= 0 || index - 1 >= m_devices.size())
        return {};                       // "" == all devices
    return m_devices.at(index - 1).id;
}

void AppController::stashCurrentProfile()
{
    Preset p = currentAsPreset();
    p.description = m_profileKey.isEmpty() ? QStringLiteral("all devices") : m_profileKey;
    m_profiles.insert(m_profileKey, p);
}

void AppController::applyProfile(const QString &key)
{
    m_profileKey = key;

    const auto it = m_profiles.constFind(key);
    if (it == m_profiles.constEnd() || !it->isValid()) {
        // No profile yet for this device -- start it from the current curve
        // rather than snapping to flat, which is the less surprising default.
        stashCurrentProfile();
        return;
    }

    m_restoring = true;
    m_bands.setBands(it->bands);
    m_preamp = std::clamp(it->preamp, -30.0, 30.0);
    m_restoring = false;
    emit preampChanged();
    emit generatedTextChanged();
}

void AppController::setPerDevice(bool on)
{
    if (m_perDevice == on)
        return;
    m_perDevice = on;
    emit perDeviceChanged();
    emit generatedTextChanged();
    scheduleWrite();
    setMessage(on ? QStringLiteral("已开启逐设备配置 —— 每个输出设备记住自己的曲线")
                  : QStringLiteral("已关闭逐设备配置 —— 所有设备共用一条曲线"));
}

void AppController::loadProfiles()
{
    QDir dir(profilesDirectory());
    if (!dir.exists())
        return;

    const QStringList files = dir.entryList({ QStringLiteral("*.peace") }, QDir::Files);
    for (const QString &file : files) {
        Preset p;
        if (!PeaceFile::read(dir.filePath(file), &p) || !p.isValid())
            continue;
        // The description carries the endpoint id; the file name is only a
        // sanitised form of it and cannot be reversed.
        const QString key = (p.description == QLatin1String("all devices")) ? QString() : p.description;
        m_profiles.insert(key, p);
    }
}

void AppController::saveProfiles() const
{
    if (m_profiles.isEmpty())
        return;

    QDir dir(profilesDirectory());
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return;

    for (auto it = m_profiles.constBegin(); it != m_profiles.constEnd(); ++it) {
        QString name = it.key().isEmpty() ? QStringLiteral("all") : it.key();
        for (QChar &c : name) {
            if (QStringLiteral("\\/:*?\"<>|").contains(c))
                c = QLatin1Char('_');
        }
        PeaceFile::write(dir.filePath(name + QStringLiteral(".peace")), it.value());
    }
}

// ------------------------------------------------------------------- AutoEQ

bool AppController::loadAutoEq()
{
    if (m_autoEq.loaded())
        return true;

    // DreamDSP's own directory first, so a copy placed there wins over whatever
    // an old Peace install left behind.
    QStringList roots;
    roots << PresetStore::userDirectory() + QStringLiteral("/autoeq");
    roots << PresetStore::userDirectory();
    if (!m_apo.configPath.isEmpty())
        roots << m_apo.configPath;

    QString err;
    if (!m_autoEq.load(roots, &err)) {
        setError(QStringLiteral("载入 AutoEQ 数据库失败: %1").arg(err));
        emit autoEqChanged();
        return false;
    }
    setError({});
    setMessage(QStringLiteral("已载入 AutoEQ 数据库 · %1 条").arg(m_autoEq.entries().size()));
    emit autoEqChanged();
    return true;
}

QVariantList AppController::searchAutoEq(const QString &needle, int limit)
{
    QVariantList out;
    if (!m_autoEq.loaded())
        return out;

    const QStringList sources = AutoEqDatabase::sourceNames();
    for (const int idx : m_autoEq.search(needle, limit)) {
        const AutoEqEntry &e = m_autoEq.entries().at(idx);
        out.append(QVariantMap{
            { QStringLiteral("index"), idx },
            { QStringLiteral("device"), e.device },
            { QStringLiteral("measurer"), e.measurer },
            { QStringLiteral("target"), e.target },
            { QStringLiteral("source"), sources.value(e.source) },
            { QStringLiteral("hasFixed"), !e.fixedBand.isEmpty() },
        });
    }
    return out;
}

bool AppController::importAutoEq(int entryIndex, bool fixedBand)
{
    if (entryIndex < 0 || entryIndex >= m_autoEq.entries().size())
        return false;

    const AutoEqEntry &e = m_autoEq.entries().at(entryIndex);
    Preset p;
    if (!AutoEqDatabase::toPreset(e, fixedBand, &p)) {
        setError(QStringLiteral("无法解析该条 AutoEQ 数据"));
        return false;
    }

    m_restoring = true;
    m_bands.setBands(p.bands);
    setPreamp(p.preamp);
    m_restoring = false;

    m_autoEqSource = e.device;
    setCurrentPreset(e.device);
    markDirty(true);
    setError({});
    setMessage(QStringLiteral("已导入「%1」· %2 段 · %3")
                   .arg(e.device).arg(p.bands.size())
                   .arg(fixedBand ? QStringLiteral("固定频段") : QStringLiteral("参数均衡")));
    scheduleWrite();
    return true;
}

// ------------------------------------------------------------------ startup

bool AppController::autostart() const
{
    return autostart::isEnabled();
}

void AppController::setAutostart(bool on)
{
    if (autostart::isEnabled() == on)
        return;

    QString err;
    if (!autostart::setEnabled(on, &err)) {
        setError(err);
        return;
    }
    setError({});
    setMessage(on ? QStringLiteral("已设置开机自启(启动时最小化到托盘)")
                  : QStringLiteral("已取消开机自启"));
    emit autostartChanged();
}

void AppController::setCloseToTray(bool on)
{
    if (m_closeToTray == on)
        return;
    m_closeToTray = on;
    emit closeToTrayChanged();
    saveSession();
}

// ------------------------------------------------------------------ presets

Preset AppController::currentAsPreset() const
{
    Preset p;
    p.name = m_currentPreset;
    p.preamp = m_preamp;
    p.bands = m_bands.bands();
    return p;
}

void AppController::loadPreset(int row)
{
    Preset p;
    QString err;
    if (!m_presets.load(row, &p, &err)) {
        setError(QStringLiteral("载入预设失败: %1").arg(err));
        return;
    }

    m_restoring = true;
    m_bands.setBands(p.bands);
    setPreamp(p.preamp);
    m_restoring = false;

    setCurrentPreset(p.name);
    markDirty(false);
    setError({});
    setMessage(QStringLiteral("已载入「%1」· %2 段").arg(p.name).arg(p.bands.size()));
    scheduleWrite();
}

bool AppController::savePreset(const QString &name)
{
    QString err;
    if (!m_presets.save(name, currentAsPreset(), &err)) {
        setError(QStringLiteral("保存预设失败: %1").arg(err));
        return false;
    }
    setCurrentPreset(name.trimmed());
    markDirty(false);
    setError({});
    setMessage(QStringLiteral("已保存「%1」").arg(name.trimmed()));
    return true;
}

bool AppController::deletePreset(int row)
{
    QString err;
    if (!m_presets.remove(row, &err)) {
        setError(err);
        return false;
    }
    setMessage(QStringLiteral("已删除预设"));
    return true;
}

// ------------------------------------------------------- graphic equalizer

bool AppController::graphicLoaded() const
{
    for (int i = 0; i < dsp::GraphicEq::kBands; ++i)
        if (m_graphic.gainDb[i] != 0.0f)
            return true;
    return false;
}

QVariantList AppController::graphicBands() const
{
    const double *f = dsp::GraphicEq::centres();
    QVariantList out;
    for (int i = 0; i < dsp::GraphicEq::kBands; ++i) {
        out.append(QVariantMap{
            { QStringLiteral("hz"), f[i] },
            // What was asked for, and what the bank settled on. Both, because
            // the gap between them is the honest description of a third-octave
            // bank following an arbitrary curve.
            { QStringLiteral("target"),
              i < m_graphicTarget.size() ? m_graphicTarget.at(i) : 0.0 },
            { QStringLiteral("gain"), double(m_graphic.gainDb[i]) },
        });
    }
    return out;
}

bool AppController::importGraphicEq(const QString &text)
{
    const GraphicCurve curve = parseGraphicCurve(text);
    if (curve.size() < 2) {
        setError(QStringLiteral("没能从这段文本里读出曲线 —— 需要「频率 增益」成对的数据"));
        return false;
    }

    double target[dsp::GraphicEq::kBands];
    resampleCurve(curve, dsp::GraphicEq::centres(), dsp::GraphicEq::kBands, target);

    m_graphicTarget.resize(dsp::GraphicEq::kBands);
    for (int i = 0; i < dsp::GraphicEq::kBands; ++i)
        m_graphicTarget[i] = target[i];

    // Fitted at the endpoint's own rate, because that is where it will run.
    const double rate = m_deviceRate > 0 ? double(m_deviceRate) : 48000.0;
    m_graphicFitError = dsp::fitGraphicEq(target, rate, m_graphic.gainDb);
    if (m_graphic.amount <= 0.0f)
        m_graphic.amount = 1.0f;
    m_graphicEnabled = true;

    setError({});
    setMessage(QStringLiteral("已载入曲线 · %1 个点 → 31 段,最大偏差 %2 dB")
                   .arg(curve.size())
                   .arg(m_graphicFitError, 0, 'f', 2));
    emit graphicChanged();
    emit chainChanged();
    markDirty(true);
    scheduleWrite();
    return true;
}

bool AppController::loadGraphicEqFile(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QStringLiteral("打不开 %1:%2").arg(path, f.errorString()));
        return false;
    }
    return importGraphicEq(QString::fromUtf8(f.readAll()));
}

void AppController::clearGraphicEq()
{
    std::memset(m_graphic.gainDb, 0, sizeof m_graphic.gainDb);
    m_graphicTarget.clear();
    m_graphicFitError = 0.0;
    m_graphicEnabled = false;
    emit graphicChanged();
    emit chainChanged();
    markDirty(true);
    scheduleWrite();
}

void AppController::setGraphicEnabled(bool on)
{
    if (m_graphicEnabled == on)
        return;
    m_graphicEnabled = on;
    emit graphicChanged();
    emit chainChanged();
    markDirty(true);
    scheduleWrite();
}

void AppController::setGraphicAmount(double v)
{
    const float a = float(std::clamp(v, 0.0, 1.0));
    if (m_graphic.amount == a)
        return;
    m_graphic.amount = a;
    emit graphicChanged();
    markDirty(true);
    scheduleWrite();
}

// ------------------------------------------------------------------- chain

namespace {

struct StageInfo {
    uint8_t id;
    const char *name;
    const char *hint;
    int page;        // which section configures it
};

// Index-aligned with the kStage* ids, so a lookup is a subscript.
const StageInfo kStages[] = {
    { dsp::kStageEqualizer,   "参数均衡",   "32 段 · 18 种滤波器",    0 },
    { dsp::kStageGraphic,     "图形均衡",   "31 段 · 从曲线导入",     0 },
    { dsp::kStageLoudness,    "等响度补偿", "跟随系统音量",           3 },
    { dsp::kStageDynBass,     "动态低音",   "按剩余余量提升低频",     1 },
    { dsp::kStageBass,        "虚拟低音",   "合成谐波,小喇叭用",     1 },
    { dsp::kStageExciter,     "激励器",     "高频谐波",               1 },
    { dsp::kStageTube,        "电子管",     "偶次谐波染色",           1 },
    { dsp::kStageComp,        "压缩器",     "动态范围",               1 },
    { dsp::kStageMultiband,   "多段压缩",   "三段独立动态",           1 },
    { dsp::kStageReverb,      "混响",       "空间感",                 1 },
    { dsp::kStageWidth,       "声场展宽",   "侧信号增益",             1 },
    { dsp::kStageCrossfeed,   "串扰",       "耳机听音箱",             1 },
    { dsp::kStageMatrix,      "声道路由",   "输出 = 输入的加权和",    3 },
    { dsp::kStageConvolution, "卷积",       "脉冲响应 · 任意采样率",  2 },
    { dsp::kStageDelay,       "声道延时",   "音箱时间对齐",           3 },
    { dsp::kStageLimiter,     "限制器",     "真峰值前瞻限制",         3 },
    { dsp::kStageTransient,   "瞬态整形",   "起音与延音,无阈值",     1 },
    { dsp::kStageClipper,     "削波器",     "软拐点 · 限制器之前",    3 },
    { dsp::kStageAutoGain,    "自动音量",   "按 BS.1770 响度找平",    3 },
    { dsp::kStageNightMode,   "夜间模式",   "杜比 DRC 曲线",          3 },
};
// Page indices follow the navigation rail: 0 equalizer, 1 effects,
// 2 convolution, 3 output, 4 chain, 5 AutoEQ, 6 settings.
static_assert(std::size(kStages) == size_t(dsp::kStageCount),
              "kStages must stay index-aligned with the kStage* ids");

} // namespace

QVariantList AppController::chain() const
{
    QVariantList out;
    for (int i = 0; i < dsp::kStageCount; ++i) {
        const uint8_t id = m_order[i];
        if (id >= dsp::kStageCount)
            continue;
        const StageInfo &s = kStages[id];
        out.append(QVariantMap{
            { QStringLiteral("id"), int(id) },
            { QStringLiteral("name"), QString::fromUtf8(s.name) },
            { QStringLiteral("hint"), QString::fromUtf8(s.hint) },
            { QStringLiteral("page"), s.page },
            { QStringLiteral("on"), stageEnabled(int(id)) },
        });
    }
    return out;
}

bool AppController::chainIsDefault() const
{
    return std::memcmp(m_order, dsp::kDefaultOrder, sizeof m_order) == 0;
}

// Both of the switches below have to name every stage, and neither the
// compiler nor the sanitiser can tell when one of them does not -- an
// unhandled id silently reads back as "off" and silently ignores being
// switched on, which is exactly the drift the chain page was built to avoid.
// This is the tripwire: adding a stage bumps kStageCount, which breaks the
// build here rather than in a bug report.
static_assert(dsp::kStageCount == 20,
              "a new stage needs a case in stageEnabled() and setStageEnabled()");

bool AppController::stageEnabled(int stageId) const
{
    switch (stageId) {
    case dsp::kStageEqualizer:   return m_eqEnabled;
    case dsp::kStageGraphic:     return m_graphicEnabled;
    case dsp::kStageLoudness:    return m_output.loudnessOn();
    case dsp::kStageDynBass:     return m_effects.dynBassOn();
    case dsp::kStageBass:        return m_effects.bassOn();
    case dsp::kStageExciter:     return m_effects.exciterOn();
    case dsp::kStageTube:        return m_effects.tubeOn();
    case dsp::kStageComp:        return m_compressor.enabled();
    case dsp::kStageMultiband:   return m_effects.multibandOn();
    case dsp::kStageReverb:      return m_reverb.enabled();
    case dsp::kStageWidth:       return m_effects.widthOn();
    case dsp::kStageCrossfeed:   return m_effects.crossfeedOn();
    case dsp::kStageMatrix:      return m_output.matrixOn();
    case dsp::kStageConvolution: return m_convolutionEnabled;
    case dsp::kStageDelay:       return m_output.delayOn();
    case dsp::kStageLimiter:     return m_output.limiterOn();
    case dsp::kStageTransient:   return m_effects.transientOn();
    case dsp::kStageClipper:     return m_effects.clipperOn();
    case dsp::kStageAutoGain:    return m_effects.autoGainOn();
    case dsp::kStageNightMode:   return m_effects.nightModeOn();
    default:                     return false;
    }
}

void AppController::setStageEnabled(int stageId, bool on)
{
    // Adding and removing an effect is the same switch its own page carries.
    // There is deliberately no second notion of membership: a chain list whose
    // entries could disagree with the switches would be two truths about the
    // same thing, and they would drift.
    switch (stageId) {
    case dsp::kStageEqualizer:   setEqEnabled(on); break;
    case dsp::kStageGraphic:     setGraphicEnabled(on); break;
    case dsp::kStageLoudness:    m_output.setLoudnessOn(on); break;
    case dsp::kStageDynBass:     m_effects.setProperty("dynBassEnabled", on); break;
    case dsp::kStageBass:        m_effects.setProperty("bassEnabled", on); break;
    case dsp::kStageExciter:     m_effects.setProperty("exciterEnabled", on); break;
    case dsp::kStageTube:        m_effects.setProperty("tubeEnabled", on); break;
    case dsp::kStageComp:        m_compressor.setEnabled(on); break;
    case dsp::kStageMultiband:   m_effects.setProperty("multibandEnabled", on); break;
    case dsp::kStageReverb:      m_reverb.setEnabled(on); break;
    case dsp::kStageWidth:       m_effects.setProperty("widthEnabled", on); break;
    case dsp::kStageCrossfeed:   m_effects.setProperty("crossfeedEnabled", on); break;
    case dsp::kStageMatrix:      m_output.setMatrixOn(on); break;
    case dsp::kStageConvolution: setConvolutionEnabled(on); break;
    case dsp::kStageDelay:       m_output.setDelayOn(on); break;
    case dsp::kStageLimiter:     m_output.setLimiterOn(on); break;
    case dsp::kStageTransient:   m_effects.setProperty("transientEnabled", on); break;
    case dsp::kStageClipper:     m_effects.setProperty("clipperEnabled", on); break;
    case dsp::kStageAutoGain:    m_effects.setProperty("autoGainEnabled", on); break;
    case dsp::kStageNightMode:   m_effects.setProperty("nightModeEnabled", on); break;
    default: return;
    }
    emit chainChanged();
}

void AppController::moveStage(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= dsp::kStageCount)
        return;
    toIndex = std::clamp(toIndex, 0, dsp::kStageCount - 1);
    if (fromIndex == toIndex)
        return;

    const uint8_t moved = m_order[fromIndex];
    if (fromIndex < toIndex)
        std::memmove(m_order + fromIndex, m_order + fromIndex + 1, size_t(toIndex - fromIndex));
    else
        std::memmove(m_order + toIndex + 1, m_order + toIndex, size_t(fromIndex - toIndex));
    m_order[toIndex] = moved;

    emit chainChanged();
    markDirty(true);
    scheduleWrite();
}

void AppController::resetChainOrder()
{
    if (chainIsDefault())
        return;
    std::memcpy(m_order, dsp::kDefaultOrder, sizeof m_order);
    emit chainChanged();
    markDirty(true);
    scheduleWrite();
    setMessage(QStringLiteral("已恢复默认处理顺序"));
}

// ------------------------------------------------------------------ private

void AppController::scheduleWrite()
{
    // The equalizer's real destination is the parameter channel, not a text
    // file, so every change goes there first and unconditionally. The publisher
    // coalesces on its own, which is why this is not behind the timer.
    m_publisher.setEqualizer(m_preamp, eqRunsHere());
    m_publisher.setGraphic(m_graphic, m_graphicEnabled);
    m_publisher.setMasterEnabled(m_masterEnabled);
    m_publisher.setOrder(m_order);
    m_publisher.schedule();

    // Started whether or not Equalizer APO is installed: this timer is also
    // what saves the session, and gating it on APO meant that on a machine
    // without APO nothing the user changed was ever written down.
    m_writeTimer.start();
}

void AppController::flushParams()
{
    m_publisher.setEqualizer(m_preamp, eqRunsHere());
    m_publisher.setGraphic(m_graphic, m_graphicEnabled);
    m_publisher.setMasterEnabled(m_masterEnabled);
    m_publisher.setOrder(m_order);
    m_publisher.publishNow();
}

bool AppController::nativeProcessing() const
{
    // DreamDSP does the work itself whenever its own processing object is
    // actually in this endpoint's chain -- which requires the endpoint to have
    // a chain at all. Holding the slot on a device with its effects switched
    // off is not processing, it is a registration nobody reads.
    return m_apoState.installed() && m_apoSlot.live();
}

bool AppController::eqRunsHere() const
{
    // Exactly one of the two applies the curve. Equalizer APO applies it for as
    // long as our include line is in its config.txt; DreamDSP applies it the
    // rest of the time. Both at once would land every boost twice, and the
    // symptom -- everything is right but twice as strong -- is the kind of thing
    // that gets blamed on the filters rather than on the routing.
    return m_eqEnabled && !(m_apo.found && m_engaged);
}

void AppController::syncEqOwnership()
{
    // Our object being in the chain is the whole point of installing it, so it
    // wins: the include line comes out and Equalizer APO stops being part of
    // the audio path. Reversible, and reported rather than silent.
    if (nativeProcessing() && m_apo.found && m_engaged) {
        setEngaged(false);
        if (!m_engaged)
            setMessage(QStringLiteral("均衡器已交给 DreamDSP 自己处理 —— "
                                      "已从 Equalizer APO 的 config.txt 移除引用"));
        // If that failed -- an unwritable config.txt is the usual reason -- the
        // include line is still there and APO is still applying the curve.
        // eqRunsHere() then keeps our own equalizer switched off, which is the
        // right way to lose that race.
    }
}

void AppController::writeNow()
{
    // Written even when nothing includes it. The file is inert on its own, and
    // having it on disk and current is what makes engaging Equalizer APO a
    // one-line change rather than a regeneration.
    if (m_apo.found) {
        const QString path = configFilePath(m_apo, QString::fromLatin1(kIncludeFile));
        QString err;
        if (!ApoConfig::writeText(path, generatedText(), &err))
            setError(QStringLiteral("写入 %1 失败: %2").arg(QString::fromLatin1(kIncludeFile), err));
        else
            setError({});
    }

    saveSession();
}

void AppController::refreshEngaged()
{
    if (!m_apo.found)
        return;

    QStringList lines;
    if (!ApoConfig::readLines(configFilePath(m_apo, QStringLiteral("config.txt")), &lines))
        return;

    const bool engaged = ApoConfig::hasInclude(lines, QString::fromLatin1(kIncludeFile));
    if (engaged != m_engaged) {
        m_engaged = engaged;
        emit engagedChanged();
    }
}

void AppController::setError(const QString &err)
{
    if (m_lastError == err)
        return;
    m_lastError = err;
    emit lastErrorChanged();
}

void AppController::setMessage(const QString &msg)
{
    m_lastMessage = msg;
    emit lastMessageChanged();
}

void AppController::setCurrentPreset(const QString &name)
{
    if (m_currentPreset == name)
        return;
    m_currentPreset = name;
    emit currentPresetChanged();
}

void AppController::markDirty(bool dirty)
{
    if (m_restoring || m_dirty == dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged();
}

// ------------------------------------------------------------------ session

QString AppController::sessionPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("session.peace"));
}

void AppController::saveSession()
{
    QSettings s(QStringLiteral("DreamDSP"), QStringLiteral("DreamDSP"));
    s.setValue(QStringLiteral("eqEnabled"), m_eqEnabled);
    s.setValue(QStringLiteral("masterEnabled"), m_masterEnabled);
    s.setValue(QStringLiteral("closeToTray"), m_closeToTray);
    s.setValue(QStringLiteral("currentPreset"), m_currentPreset);
    s.setValue(QStringLiteral("deviceId"),
               (m_currentDevice > 0 && m_currentDevice - 1 < m_devices.size())
                   ? m_devices.at(m_currentDevice - 1).id
                   : QString());

    // Bands and preamp go into a real .peace file rather than QSettings: it is
    // the same format as a preset, so the session is directly loadable and
    // human-inspectable.
    Preset p = currentAsPreset();
    p.description = QStringLiteral("DreamDSP session state");
    PeaceFile::write(sessionPath(), p);

    s.setValue(QStringLiteral("perDevice"), m_perDevice);
    s.setValue(QStringLiteral("convolutionFile"), m_convolution.path);
    s.setValue(QStringLiteral("convolutionEnabled"), m_convolutionEnabled);
    m_hotkeys.save(s);

    if (m_perDevice) {
        const_cast<AppController *>(this)->stashCurrentProfile();
        saveProfiles();
    }
}

void AppController::restoreSession()
{
    m_restoring = true;

    QSettings s(QStringLiteral("DreamDSP"), QStringLiteral("DreamDSP"));
    m_eqEnabled = s.value(QStringLiteral("eqEnabled"), true).toBool();
    m_masterEnabled = s.value(QStringLiteral("masterEnabled"), true).toBool();
    m_closeToTray = s.value(QStringLiteral("closeToTray"), true).toBool();
    m_perDevice = s.value(QStringLiteral("perDevice"), false).toBool();
    m_hotkeys.load(s);

    // Re-read the header rather than trusting stored metadata: the file may
    // have been replaced or removed since last run.
    const QString convFile = s.value(QStringLiteral("convolutionFile")).toString();
    if (!convFile.isEmpty() && QFileInfo::exists(convFile)) {
        readWaveHeader(convFile, &m_convolution);
        m_convolutionEnabled = s.value(QStringLiteral("convolutionEnabled"), false).toBool();
    }
    m_currentPreset = s.value(QStringLiteral("currentPreset")).toString();
    m_pendingDeviceId = s.value(QStringLiteral("deviceId")).toString();

    Preset p;
    if (QFileInfo::exists(sessionPath()) && PeaceFile::read(sessionPath(), &p) && p.isValid()) {
        m_bands.setBands(p.bands);
        m_preamp = std::clamp(p.preamp, -30.0, 30.0);
    }

    m_restoring = false;

    emit eqEnabledChanged();
    emit preampChanged();
    emit currentPresetChanged();
    emit generatedTextChanged();
}

} // namespace dreamdsp
