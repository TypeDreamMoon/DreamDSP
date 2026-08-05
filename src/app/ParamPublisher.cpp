#include "app/ParamPublisher.h"

#include "BiquadFilter.h"
#include "ImpulseBlob.h"
#include "WavFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <cmath>
#include <cstring>
#include <vector>

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
                                const EffectsModel *effects,
                                const dsp::Convolution::Params &convolution,
                                bool convolutionEnabled)
{
    // From transparentBlock, not from a value-initialised ParamBlock: the
    // Params members carry default member initialisers, so `{}` would produce a
    // block with the reverb already at wet 0.3.
    dsp::ParamBlock b = dsp::transparentBlock();

    // Parameters are carried unconditionally; only the mask depends on whether
    // an effect is switched on.
    //
    // Copying them only for enabled effects seemed harmless -- the APO ignores
    // the rest -- but this block doubles as the session store, so a disabled
    // effect's settings were being replaced by the sanitiser's fallbacks and
    // read back as those on the next launch. Switching an effect off lost
    // everything you had set on it, and the values it came back with were not
    // even the model's defaults.
    if (comp) {
        b.comp = comp->dspParams();
        if (comp->enabled())
            b.enableMask |= dsp::kEnComp;
    }
    if (reverb) {
        b.reverb = reverb->dspParams();
        if (reverb->enabled())
            b.enableMask |= dsp::kEnReverb;
    }
    if (effects) {
        b.bass = effects->bassParams();
        b.exciter = effects->exciterParams();
        b.tube = effects->tubeParams();
        b.multiband = effects->multibandParams();
        b.width = effects->widthParams();
        b.crossfeed = effects->crossfeedParams();

        if (effects->bassOn())      b.enableMask |= dsp::kEnBass;
        if (effects->exciterOn())   b.enableMask |= dsp::kEnExciter;
        if (effects->tubeOn())      b.enableMask |= dsp::kEnTube;
        if (effects->multibandOn()) b.enableMask |= dsp::kEnMultiband;
        if (effects->widthOn())     b.enableMask |= dsp::kEnWidth;
        if (effects->crossfeedOn()) b.enableMask |= dsp::kEnCrossfeed;
    }

    // Carried whether or not it is enabled: the impulse response's identity is
    // what tells the APO to arm, and arming decides the latency it reports for
    // the whole life of the stream. Switching convolution off should not undo
    // that, or every toggle would change the reported delay.
    b.convolution = convolution;
    if (convolutionEnabled && convolution.irGeneration != 0u)
        b.enableMask |= dsp::kEnConvolution;

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

    // Not value-initialised: the members carry defaults that are right, but
    // irGeneration must start at zero -- it is what tells the APO whether an
    // impulse response has ever been configured.
    m_convolution = dsp::Convolution::Params{};

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

    dsp::ParamBlock b = blockFromModels(m_comp, m_reverb, m_effects,
                                        m_convolution, m_convolutionEnabled);
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

QString ParamPublisher::impulseDirectory()
{
    return controlDirectory() + QStringLiteral("/ir");
}

bool ParamPublisher::publishImpulse(const QString &wavPath, QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };

    dsp::WavData wav;
    std::string decodeError;
    if (!dsp::readWav(wavPath.toStdString(), &wav, &decodeError)) {
        return fail(QStringLiteral("无法读取:%1")
                        .arg(QString::fromStdString(decodeError)));
    }
    if (wav.frames() <= 0 || wav.channelCount() <= 0)
        return fail(QStringLiteral("文件里没有音频"));
    if (wav.channelCount() > int(dsp::kConvMaxChannels))
        return fail(QStringLiteral("声道数 %1 超出上限 8").arg(wav.channelCount()));
    if (quint64(wav.frames()) > dsp::kIrMaxFrames)
        return fail(QStringLiteral("脉冲响应过长(%1 帧)").arg(wav.frames()));
    if (wav.sampleRate < 8000 || wav.sampleRate > 384000)
        return fail(QStringLiteral("采样率 %1 Hz 不在支持范围").arg(wav.sampleRate));

    const uint32_t frames = uint32_t(wav.frames());
    const uint32_t channels = uint32_t(wav.channelCount());

    // Planar, which is what the blob format stores and what the convolver
    // wants; readWav already delivers it that way.
    std::vector<float> planar(size_t(frames) * channels, 0.0f);
    for (uint32_t c = 0; c < channels; ++c) {
        std::memcpy(planar.data() + size_t(c) * frames,
                    wav.channels[c].data(), size_t(frames) * sizeof(float));
    }

    // Remove any DC offset, which is a recording artefact rather than part of
    // the response. The sum of the taps is the filter's gain at DC, so over a
    // long impulse response a per-sample offset of a fraction of an LSB
    // integrates into tens of decibels of gain that nothing downstream expects.
    //
    // Note this is not the same thing as an impulse response legitimately
    // having gain at DC -- every lowpass-shaped one does, and refusing those
    // would reject most of the corpus. Only the offset is removed.
    //
    // 10 Hz, second order, and forward only rather than zero-phase: filtering
    // in both directions at 10 Hz would spread pre-ringing over tens of
    // milliseconds *before* the direct sound, and pre-echo is the artefact
    // hearing forgives least. A forward-only phase shift at 10 Hz is inaudible.
    {
        dsp::BiquadFilter dcBlocker;
        for (uint32_t c = 0; c < channels; ++c) {
            dcBlocker.design(dsp::BiquadFilter::Kind::HighPass, 10.0,
                             0.70710678118654752, 0.0, double(wav.sampleRate));
            dcBlocker.reset();
            float *p = planar.data() + size_t(c) * frames;
            for (uint32_t i = 0; i < frames; ++i)
                p[i] = dcBlocker.process(p[i]);
        }
    }

    const dsp::IrBlobHeader header =
        dsp::makeIrBlobHeader(planar.data(), frames, channels, uint32_t(wav.sampleRate));

    QString hex;
    hex.reserve(32);
    for (int i = 0; i < 16; ++i)
        hex += QStringLiteral("%1").arg(header.hash[i], 2, 16, QLatin1Char('0'));

    QDir().mkpath(impulseDirectory());
    const QString blobPath = impulseDirectory() + QLatin1Char('/') + hex
                             + QStringLiteral(".irb");

    // Written before the parameters that name it, and only if it is not already
    // there -- the name is a hash of the contents, so an existing file with
    // this name already holds exactly these samples.
    if (!QFile::exists(blobPath)) {
        QSaveFile blob(blobPath);
        if (!blob.open(QIODevice::WriteOnly))
            return fail(QStringLiteral("无法写入 %1").arg(blobPath));
        blob.write(reinterpret_cast<const char *>(&header), sizeof header);
        blob.write(reinterpret_cast<const char *>(planar.data()),
                   qint64(planar.size() * sizeof(float)));
        if (!blob.commit())
            return fail(QStringLiteral("无法写入 %1").arg(blobPath));
    }

    std::memcpy(m_convolution.irHash, header.hash, 16);
    m_convolution.irRateHz = header.sampleRate;
    m_convolution.irFrames = header.frames;
    m_convolution.irChannels = header.channels;
    // Bumped rather than set, so that reselecting the same file still counts as
    // a change the APO will act on.
    ++m_convolution.irGeneration;
    m_impulseName = QFileInfo(wavPath).completeBaseName();

    publishNow();
    return true;
}

void ParamPublisher::clearImpulse()
{
    m_convolution = dsp::Convolution::Params{};
    m_convolutionEnabled = false;
    m_impulseName.clear();
    publishNow();
}

void ParamPublisher::setConvolutionEnabled(bool on)
{
    if (m_convolutionEnabled == on)
        return;
    m_convolutionEnabled = on;
    schedule();
}

void ParamPublisher::setConvolutionMix(double mix)
{
    m_convolution.mix = float(qBound(0.0, mix, 1.0));
    schedule();
}

void ParamPublisher::setConvolutionTrimDb(double db)
{
    m_convolution.trimDb = float(qBound(-24.0, db, 12.0));
    schedule();
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

    m_generation = b.generation;

    // params.bin doubles as the session store for the effects.
    //
    // It is already the authoritative wire format, already versioned, already
    // sanitised, and already exactly the state the DSP runs -- so restoring
    // from it means what comes back is byte-for-byte what was last in effect,
    // with no second serialisation to keep in step.
    //
    // Without this, every launch started with all effects off and immediately
    // published that, wiping whatever was running. The same happened to any
    // second instance, including the diagnostic ones.
    if (m_comp)
        m_comp->restore(b.comp, (b.enableMask & dsp::kEnComp) != 0);
    if (m_reverb)
        m_reverb->restore(b.reverb, (b.enableMask & dsp::kEnReverb) != 0);
    if (m_effects)
        m_effects->restore(b);

    m_convolution = b.convolution;
    m_convolutionEnabled = (b.enableMask & dsp::kEnConvolution) != 0;
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
