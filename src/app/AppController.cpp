#include "app/AppController.h"

#include "core/ApoConfig.h"
#include "platform/Autostart.h"

#include <QCoreApplication>
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

    restoreSession();
    refreshDevices();
    refreshEngaged();

    // Make sure our include file exists from the start; it is inert until the
    // user engages it, so this cannot change what they hear.
    if (m_apo.found)
        writeNow();
    markDirty(false);
}

AppController::~AppController()
{
    // Losing the last slider move because the process exited would be a
    // genuinely annoying bug.
    if (m_writeTimer.isActive()) {
        m_writeTimer.stop();
        writeNow();
    }
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
    refreshTrayIcon();
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

QString AppController::generatedText() const
{
    QString out;
    out += QStringLiteral("# Generated by DreamDSP %1 -- edits will be overwritten.\r\n")
               .arg(QStringLiteral(DREAMDSP_VERSION));

    if (!m_eqEnabled) {
        out += QStringLiteral("# (equalizer disabled)\r\n");
        return out;
    }

    const QString conv = (m_convolutionEnabled && !m_convolution.path.isEmpty())
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

    emit devicesChanged();
    emit currentDeviceChanged();
    emit convolutionChanged();
}

void AppController::resetAll()
{
    m_bands.zeroGains();
    setPreamp(0.0);
    setMessage(QStringLiteral("已全部归零"));
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
    p.setBrush(m_eqEnabled ? on : off);
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
    m_tray.setToolTip(m_eqEnabled
                          ? QStringLiteral("DreamDSP — 均衡器已启用%1")
                                .arg(m_engaged ? QString() : QStringLiteral(" (未接管)"))
                          : QStringLiteral("DreamDSP — 均衡器已关闭"));
}

void AppController::quitApplication()
{
    flushNow();
    m_tray.uninstall();
    QCoreApplication::quit();
}

// -------------------------------------------------------------- convolution

bool AppController::rateMismatch() const
{
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

bool AppController::selectImpulse(int index)
{
    if (index < 0 || index >= m_impulses.size())
        return false;

    m_convolution = m_impulses.at(index);
    m_convolutionEnabled = true;

    emit convolutionChanged();
    emit generatedTextChanged();
    scheduleWrite();

    if (rateMismatch()) {
        setError(QStringLiteral("采样率不匹配:该脉冲响应是 %1 Hz,设备是 %2 Hz —— APO 要求两者一致,卷积不会正确工作")
                     .arg(m_convolution.sampleRate).arg(m_deviceRate));
    } else {
        setError({});
        setMessage(QStringLiteral("已应用卷积「%1」").arg(m_convolution.name));
    }
    return true;
}

void AppController::clearConvolution()
{
    m_convolution = ImpulseResponse{};
    m_convolutionEnabled = false;
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

    QString err;
    if (!m_autoEq.load(m_apo.configPath, &err)) {
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

// ------------------------------------------------------------------ private

void AppController::scheduleWrite()
{
    if (m_apo.found)
        m_writeTimer.start();
}

void AppController::writeNow()
{
    if (!m_apo.found)
        return;

    const QString path = configFilePath(m_apo, QString::fromLatin1(kIncludeFile));
    QString err;
    if (!ApoConfig::writeText(path, generatedText(), &err))
        setError(QStringLiteral("写入 %1 失败: %2").arg(QString::fromLatin1(kIncludeFile), err));
    else
        setError({});

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
