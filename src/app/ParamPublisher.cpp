#include "app/ParamPublisher.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <cstring>

namespace dreamdsp {

namespace {

// Long enough to collapse a slider drag into a handful of writes, short enough
// that letting go feels immediate. The APO adds up to its own poll interval on
// top of this.
constexpr int kWriteDebounceMs = 30;
constexpr int kStatusPollMs = 1000;

} // namespace

dsp::ParamBlock blockFromModels(const CompressorModel *comp,
                                const ReverbModel *reverb,
                                const EffectsModel *effects)
{
    // From transparentBlock, not from a value-initialised ParamBlock: the
    // Params members carry default member initialisers, so `{}` would produce a
    // block with the reverb already at wet 0.3.
    dsp::ParamBlock b = dsp::transparentBlock();

    if (comp && comp->enabled()) {
        b.enableMask |= dsp::kEnComp;
        b.comp = comp->dspParams();
    }
    if (reverb && reverb->enabled()) {
        b.enableMask |= dsp::kEnReverb;
        b.reverb = reverb->dspParams();
    }
    if (effects) {
        if (effects->bassOn())      { b.enableMask |= dsp::kEnBass;      b.bass = effects->bassParams(); }
        if (effects->exciterOn())   { b.enableMask |= dsp::kEnExciter;   b.exciter = effects->exciterParams(); }
        if (effects->tubeOn())      { b.enableMask |= dsp::kEnTube;      b.tube = effects->tubeParams(); }
        if (effects->multibandOn()) { b.enableMask |= dsp::kEnMultiband; b.multiband = effects->multibandParams(); }
        if (effects->widthOn())     { b.enableMask |= dsp::kEnWidth;     b.width = effects->widthParams(); }
        if (effects->crossfeedOn()) { b.enableMask |= dsp::kEnCrossfeed; b.crossfeed = effects->crossfeedParams(); }
    }

    // Sanitised on the way out as well as on the way in. The GUI is not the
    // threat model -- the point is that what gets written is byte-identical to
    // what the APO will install, so a round trip through the file is a fixed
    // point and the tests can assert it.
    return dsp::sanitise(b);
}

ParamPublisher::ParamPublisher(QObject *parent)
    : QObject(parent)
{
    std::memset(&m_status, 0, sizeof m_status);

    m_writeTimer.setSingleShot(true);
    m_writeTimer.setInterval(kWriteDebounceMs);
    connect(&m_writeTimer, &QTimer::timeout, this, [this] { publishNow(); });

    m_statusTimer.setInterval(kStatusPollMs);
    connect(&m_statusTimer, &QTimer::timeout, this, &ParamPublisher::pollStatus);
    m_statusTimer.start();
}

QString ParamPublisher::controlDirectory()
{
    // Beside the staged APO, which is where LOCAL SERVICE already has access.
    return QStringLiteral("C:/ProgramData/DreamDSP/control");
}

QString ParamPublisher::paramFilePath()
{
    return controlDirectory() + QStringLiteral("/params.bin");
}

QString ParamPublisher::statusFilePath()
{
    // The status file is written by the APO, i.e. by audiodg, and deliberately
    // sits outside the directory being watched for parameter changes -- writing
    // it into that directory would wake the watcher with its own output.
    return QStringLiteral("C:/ProgramData/DreamDSP/status.bin");
}

void ParamPublisher::setSources(CompressorModel *comp, ReverbModel *reverb,
                                EffectsModel *effects)
{
    m_comp = comp;
    m_reverb = reverb;
    m_effects = effects;
}

void ParamPublisher::schedule()
{
    m_writeTimer.start();
}

bool ParamPublisher::publishNow()
{
    m_writeTimer.stop();

    dsp::ParamBlock b = blockFromModels(m_comp, m_reverb, m_effects);
    b.generation = ++m_generation;

    QDir().mkpath(controlDirectory());

    // QSaveFile writes a temporary and renames it into place, so a reader
    // either sees the whole previous block or the whole new one. There is no
    // window in which a half-written file exists under the real name, and
    // nothing to repair if this process is killed here.
    QSaveFile file(paramFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = file.errorString();
        return false;
    }
    if (file.write(reinterpret_cast<const char *>(&b), sizeof b) != qint64(sizeof b)) {
        m_lastError = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        m_lastError = file.errorString();
        return false;
    }

    m_lastError.clear();
    emit published();
    return true;
}

void ParamPublisher::load()
{
    QFile file(paramFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    dsp::ParamBlock b;
    const qint64 got = file.read(reinterpret_cast<char *>(&b), sizeof b);
    file.close();
    if (got != qint64(sizeof b) || b.magic != dsp::kParamMagic
        || b.version != dsp::kParamVersion || b.sizeBytes != sizeof b) {
        return;
    }

    // Only the generation is taken. The parameters themselves come from the
    // session state the models already restore; reading them back here would
    // give the file authority over the models, and then a stale file would
    // quietly overwrite what the user last set.
    m_generation = b.generation;
}

void ParamPublisher::pollStatus()
{
    QFile file(statusFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (m_statusFresh) {
            m_statusFresh = false;
            emit statusChanged();
        }
        return;
    }

    dsp::StatusBlock s;
    const qint64 got = file.read(reinterpret_cast<char *>(&s), sizeof s);
    file.close();

    if (got != qint64(sizeof s) || s.magic != dsp::kStatusMagic
        || s.version != dsp::kStatusVersion || s.sizeBytes != sizeof s
        || s.instanceCount > 8) {
        if (m_statusFresh) {
            m_statusFresh = false;
            emit statusChanged();
        }
        return;
    }

    const bool changed = !m_statusFresh
                         || std::memcmp(&s, &m_status, sizeof s) != 0;
    m_status = s;
    m_statusFresh = true;
    if (changed)
        emit statusChanged();
}

} // namespace dreamdsp
