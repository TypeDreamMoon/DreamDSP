#include "app/ParamPublisher.h"

#include "BiquadFilter.h"
#include "ImpulseBlob.h"
#include "WavFile.h"

#include <QDateTime>
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

// How old the APO's report may be and still count as a description of now.
// Generous: it is rewritten on every stream event and at least every few
// seconds while one is open, so anything past this means no stream is running
// -- or that nothing is loading the object at all.
constexpr int kStatusStaleSeconds = 15;

} // namespace

dsp::Equalizer::Params eqParamsFromBands(const QVector<PresetBand> &bands, double preampDb)
{
    dsp::Equalizer::Params eq;
    std::memset(&eq, 0, sizeof eq);
    eq.preampDb = float(preampDb);

    int n = int(bands.size());
    if (n > dsp::Equalizer::kMaxBands)
        n = dsp::Equalizer::kMaxBands;

    for (int i = 0; i < n; ++i) {
        const PresetBand &s = bands[i];
        dsp::EqBand &d = eq.band[i];
        d.freqHz = float(s.frequency);
        // Carried even for the types that ignore it, so that switching a band
        // to LPQ and back does not lose the gain the user had dialled in. The
        // DSP layer is the one that decides a type has no gain.
        d.gainDb = float(s.gainDb);
        d.q = float(s.q);
        d.type = uint8_t(s.type);
        d.enabled = s.enabled ? 1u : 0u;
        d.reserved = 0;
    }
    eq.bandCount = uint32_t(n);
    return eq;
}

dsp::ParamBlock blockFromModels(const CompressorModel *comp,
                                const ReverbModel *reverb,
                                const EffectsModel *effects,
                                const dsp::Convolution::Params &convolution,
                                bool convolutionEnabled,
                                const EqBandModel *bands,
                                double preampDb,
                                bool eqEnabled,
                                const OutputModel *output)
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
        b.dynBass = effects->dynBassParams();
        b.transient = effects->transientParams();
        b.clipper = effects->clipperParams();
        b.autoGain = effects->autoGainParams();
        b.nightMode = effects->nightModeParams();

        if (effects->transientOn()) b.enableMask |= dsp::kEnTransient;
        if (effects->clipperOn())   b.enableMask |= dsp::kEnClipper;
        if (effects->autoGainOn())  b.enableMask |= dsp::kEnAutoGain;
        if (effects->nightModeOn()) b.enableMask |= dsp::kEnNightMode;
        if (effects->bassOn())      b.enableMask |= dsp::kEnBass;
        if (effects->exciterOn())   b.enableMask |= dsp::kEnExciter;
        if (effects->tubeOn())      b.enableMask |= dsp::kEnTube;
        if (effects->multibandOn()) b.enableMask |= dsp::kEnMultiband;
        if (effects->widthOn())     b.enableMask |= dsp::kEnWidth;
        if (effects->crossfeedOn()) b.enableMask |= dsp::kEnCrossfeed;
        if (effects->dynBassOn())   b.enableMask |= dsp::kEnDynBass;
    }

    // Carried unconditionally, for the same reason as the effects above: this
    // block is the session store, and a bypassed equalizer must come back with
    // its bands intact rather than with the sanitiser's fallbacks.
    if (bands)
        b.eq = eqParamsFromBands(bands->bands(), preampDb);
    else
        b.eq.preampDb = float(preampDb);
    if (eqEnabled)
        b.enableMask |= dsp::kEnEqualizer;

    // Same rule again: the routing, the delays and the loudness settings are
    // carried whether or not their switches are on, because this block is where
    // they are kept between runs.
    if (output) {
        b.matrix = output->matrixParams();
        b.delay = output->delayParams();
        b.loudness = output->loudnessParams();
        b.limiter = output->limiterParams();
        b.enableMask |= output->enableBits();
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
    std::memcpy(m_order, dsp::kDefaultOrder, sizeof m_order);
    std::memset(&m_graphic, 0, sizeof m_graphic);
    m_graphic.amount = 1.0f;

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
                                EffectsModel *effects, EqBandModel *bands,
                                OutputModel *output)
{
    m_comp = comp;
    m_reverb = reverb;
    m_effects = effects;
    m_bands = bands;
    m_output = output;
}

void ParamPublisher::setEqualizer(double preampDb, bool enabled)
{
    m_preampDb = preampDb;
    m_eqEnabled = enabled;
}

void ParamPublisher::setMasterEnabled(bool enabled)
{
    m_masterEnabled = enabled;
}

void ParamPublisher::setGraphic(const dsp::GraphicEq::Params &p, bool enabled)
{
    m_graphic = p;
    m_graphicEnabled = enabled;
}

void ParamPublisher::setOrder(const uint8_t *order)
{
    if (order)
        std::memcpy(m_order, order, sizeof m_order);
}

void ParamPublisher::schedule()
{
    m_writeTimer.start();
}

bool ParamPublisher::publishNow()
{
    m_writeTimer.stop();

    dsp::ParamBlock b = blockFromModels(m_comp, m_reverb, m_effects,
                                        m_convolution, m_convolutionEnabled,
                                        m_bands, m_preampDb, m_eqEnabled, m_output);
    // The curve and the order are the controller's, so they are stamped on here
    // rather than reached for inside blockFromModels -- which the offline
    // renderer also calls, with neither of them.
    b.graphic = m_graphic;
    if (m_graphicEnabled)
        b.enableMask |= dsp::kEnGraphic;
    std::memcpy(b.order, m_order, sizeof b.order);

    // A flag, not an empty mask. The mask has to keep saying which effects are
    // switched on, because this file is also where that is remembered.
    if (!m_masterEnabled)
        b.flags |= dsp::kPfBypass;

    b = dsp::sanitise(b);
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
    std::memset(&b, 0, sizeof b);
    const qint64 got = file.read(reinterpret_cast<char *>(&b), sizeof b);
    file.close();

    if (got < 4 || b.magic != dsp::kParamMagic)
        return;

    // Older versions are accepted and migrated, not rejected.
    //
    // Refusing them instead would silently reset every effect the user had
    // switched on, on the one launch where they least expect it, and this file
    // is the only place those settings are kept.
    //
    // Through v5 every addition was *appended* to the block, so an older file
    // was byte-for-byte a current one with the newer stages left zeroed and a
    // truncation check was enough. v6 broke that: three of the existing Params
    // structs grew a field, so everything after the exciter sits at a different
    // offset and a v5 file read straight into a v6 block would put the stereo
    // widener's numbers into the crossfeed. Migration is therefore member by
    // member, against the v5 offsets -- which v2, v3 and v4 are all prefixes
    // of, so one table covers every legacy version.
    //
    // Nothing before v2 is accepted: v1 predates field additions this table
    // cannot express.
    struct Legacy { uint32_t version; qint64 size; };
    static const Legacy kLegacy[] = {
        { 2u, 300 },    // before the equalizer
        { 3u, 820 },    // before routing, delay and loudness
        { 4u, 1124 },   // before the limiter, dynamic bass, graphic eq, order
        { 5u, 1300 },   // before the transient shaper, clipper, auto volume and night mode
    };

    // v6 and v7 have identical layouts -- v7 only gave the header's reserved
    // word a meaning -- so a v6 file needs its version stamped forward and its
    // flags cleared, not a field-by-field migration.
    if (got == qint64(sizeof b) && b.version == 6u && b.sizeBytes == sizeof b) {
        b.version = dsp::kParamVersion;
        b.flags = 0u;
    }

    bool ok = (got == qint64(sizeof b) && b.version == dsp::kParamVersion
               && b.sizeBytes == sizeof b);
    bool migrated = false;
    for (const Legacy &l : kLegacy) {
        if (!ok && got == l.size && b.version == l.version && b.sizeBytes == uint32_t(l.size)) {
            ok = true;
            migrated = true;
            break;
        }
    }
    if (!ok)
        return;

    if (migrated) {
        // Read before the header is rewritten, or the test below would be
        // asking the new version number what the old one supported.
        const uint32_t wrote = b.version;

        // The bytes as they were laid out by the writing version. `b` is about
        // to be rebuilt from them, so they have to be taken out of it first.
        // Two arguments: one would declare a function. Ninth time in this tree.
        std::vector<unsigned char> raw(size_t(got), 0u);
        std::memcpy(raw.data(), &b, raw.size());
        std::memset(&b, 0, sizeof b);

        struct Field { void *dst; size_t oldOffset; size_t bytes; };
        const Field fields[] = {
            { &b.generation,  12,  4 },
            { &b.enableMask,  16,  4 },
            { &b.comp,        24,  28 },
            { &b.reverb,      52,  32 },
            { &b.tube,        84,  16 },
            { &b.exciter,    100,  12 },   // v6 appended thresholdDb
            { &b.bass,       112,  16 },
            { &b.width,      128,   8 },   // v6 appended phaseAmount
            { &b.crossfeed,  136,  12 },
            { &b.multiband,  148, 108 },
            { &b.convolution,256,  44 },
            { &b.eq,         300, 520 },
            { &b.matrix,     820, 256 },
            { &b.delay,     1076,  32 },
            { &b.loudness,  1108,  16 },
            { &b.limiter,   1124,  16 },   // v6 appended truePeak
            { &b.dynBass,   1140,  16 },
            { &b.graphic,   1156, 128 },
            { &b.order,     1284,  16 },   // v5 had 16 stages, v6 has 20
        };
        for (const Field &f : fields) {
            if (f.oldOffset + f.bytes <= raw.size())
                std::memcpy(f.dst, raw.data() + f.oldOffset, f.bytes);
        }

        // The four stages v5 did not have, slotted into whatever order the user
        // had arranged the other sixteen into.
        //
        // Keeping their arrangement matters -- the old stage ids did not move,
        // so throwing it away would be a needless loss. But appending the new
        // four on the end is not neutral either: it would put the leveller and
        // the clipper *after* the limiter, which is backwards for both. So each
        // one is placed next to the stage it belongs beside, and only falls back
        // to the end if that stage somehow is not in the list.
        if (wrote >= 5u) {
            struct Placement { uint8_t stage; uint8_t anchor; bool before; };
            static const Placement kPlace[] = {
                // Nothing to anchor to -- levelling goes first, so everything
                // downstream sees a predictable level.
                { dsp::kStageAutoGain,   0xFFu,               true },
                { dsp::kStageTransient,  dsp::kStageComp,     true },
                { dsp::kStageNightMode,  dsp::kStageMultiband, false },
                { dsp::kStageClipper,    dsp::kStageLimiter,  true },
            };

            uint8_t built[dsp::kStageCount];
            int n = 0;
            for (int i = 0; i < 16; ++i)
                built[n++] = b.order[i];

            for (const Placement &p2 : kPlace) {
                int at = n;                    // fall back to the end
                if (p2.anchor == 0xFFu) {
                    at = 0;
                } else {
                    for (int i = 0; i < n; ++i) {
                        if (built[i] == p2.anchor) {
                            at = p2.before ? i : i + 1;
                            break;
                        }
                    }
                }
                for (int i = n; i > at; --i)
                    built[i] = built[i - 1];
                built[at] = p2.stage;
                ++n;
            }
            std::memcpy(b.order, built, sizeof b.order);
        }

        // Fields that v6 added inside existing structs. Zero is a legitimate
        // number for most of them and the wrong one for all of them, so each
        // gets the default the interface would have offered.
        b.exciter.thresholdDb = dsp::Exciter::Params{}.thresholdDb;
        b.width.phaseAmount = dsp::StereoWidener::Params{}.phaseAmount;
        b.limiter.truePeak = dsp::Limiter::Params{}.truePeak;
        b.transient = dsp::TransientShaper::Params{};
        b.clipper = dsp::SoftClipper::Params{};
        b.autoGain = dsp::LoudnessLeveller::Params{};
        b.nightMode = dsp::DynamicRange::Params{};

        b.version = dsp::kParamVersion;
        b.sizeBytes = uint32_t(sizeof b);

        // Bits the writing version did not have cannot have been set by it, and
        // a stage whose parameters arrived as zeros must not come up enabled --
        // the matrix in particular would be silence rather than a no-op, which
        // is why it is given the identity here rather than left as read.
        uint32_t keep = dsp::kEnBass | dsp::kEnExciter | dsp::kEnTube | dsp::kEnComp
                        | dsp::kEnMultiband | dsp::kEnReverb | dsp::kEnWidth
                        | dsp::kEnCrossfeed | dsp::kEnConvolution;
        if (wrote >= 3u)
            keep |= dsp::kEnEqualizer;
        if (wrote >= 4u)
            keep |= dsp::kEnMatrix | dsp::kEnDelay | dsp::kEnLoudness;
        if (wrote >= 5u)
            keep |= dsp::kEnLimiter | dsp::kEnDynBass | dsp::kEnGraphic;
        b.enableMask &= keep;
        if (wrote < 4u)
            b.matrix = dsp::ChannelMatrix::identity();
        // Same reasoning one version on: zeros are not a configuration.
        if (wrote < 5u) {
            b.limiter = dsp::Limiter::Params{};
            b.dynBass = dsp::DynamicBass::Params{};
            std::memset(&b.graphic, 0, sizeof b.graphic);
            b.graphic.amount = 1.0f;
            std::memcpy(b.order, dsp::kDefaultOrder, sizeof b.order);
        }
        b = dsp::sanitise(b);
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

    if (m_output)
        m_output->restore(b);

    m_graphic = b.graphic;
    if (!(m_graphic.amount > 0.0f))
        m_graphic.amount = 1.0f;
    m_graphicEnabled = (b.enableMask & dsp::kEnGraphic) != 0;
    std::memcpy(m_order, b.order, sizeof m_order);

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

    // A parseable status file is not a live one. The APO writes this while it
    // is streaming and simply stops when it is not, so the last block it wrote
    // stays on disk indefinitely -- and reporting on it is how "正在生效" came
    // to be displayed for nineteen days after the object had stopped being
    // loaded at all. The one number that could have told the truth was already
    // in the block and unread.
    const QDateTime written = QFileInfo(statusFilePath()).lastModified();
    const bool live = written.isValid()
                      && written.secsTo(QDateTime::currentDateTime()) <= kStatusStaleSeconds;
    if (!live) {
        if (m_statusFresh) {
            m_statusFresh = false;
            emit statusChanged();
        }
        m_status = s;
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
