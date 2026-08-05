#include "core/DreamPreset.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace dreamdsp {

namespace {

constexpr int kFormatVersion = 1;

// Written out field by field rather than dumped as bytes. The struct's binary
// layout is an internal detail that is allowed to change with a version bump;
// a preset file people share is not.
QJsonObject compressorToJson(const dsp::Compressor::Params &p)
{
    return QJsonObject{
        { QStringLiteral("thresholdDb"), p.thresholdDb },
        { QStringLiteral("ratio"), p.ratio },
        { QStringLiteral("kneeDb"), p.kneeDb },
        { QStringLiteral("attackMs"), p.attackMs },
        { QStringLiteral("releaseMs"), p.releaseMs },
        { QStringLiteral("makeupDb"), p.makeupDb },
        { QStringLiteral("autoKnee"), p.autoKnee },
        { QStringLiteral("autoAttack"), p.autoAttack },
        { QStringLiteral("autoRelease"), p.autoRelease },
        { QStringLiteral("autoMakeup"), p.autoMakeup },
    };
}

void compressorFromJson(const QJsonObject &o, dsp::Compressor::Params *p)
{
    p->thresholdDb = float(o.value(QStringLiteral("thresholdDb")).toDouble(p->thresholdDb));
    p->ratio = float(o.value(QStringLiteral("ratio")).toDouble(p->ratio));
    p->kneeDb = float(o.value(QStringLiteral("kneeDb")).toDouble(p->kneeDb));
    p->attackMs = float(o.value(QStringLiteral("attackMs")).toDouble(p->attackMs));
    p->releaseMs = float(o.value(QStringLiteral("releaseMs")).toDouble(p->releaseMs));
    p->makeupDb = float(o.value(QStringLiteral("makeupDb")).toDouble(p->makeupDb));
    p->autoKnee = o.value(QStringLiteral("autoKnee")).toBool(p->autoKnee);
    p->autoAttack = o.value(QStringLiteral("autoAttack")).toBool(p->autoAttack);
    p->autoRelease = o.value(QStringLiteral("autoRelease")).toBool(p->autoRelease);
    p->autoMakeup = o.value(QStringLiteral("autoMakeup")).toBool(p->autoMakeup);
}

// The band type travels as the token Equalizer APO itself uses -- PK, LSC,
// BWLP and the rest. It is the name already written into every config file and
// every .peace preset, so a hand-edited preset stays consistent with them.
FilterType filterTypeFromName(const QString &s)
{
    const QByteArray want = s.toLatin1();
    for (int i = 0; i < int(FilterType::Count); ++i) {
        const auto t = FilterType(i);
        if (want == apoToken(t))
            return t;
    }
    return FilterType::PK;
}

} // namespace

bool saveDreamPreset(const QString &path, const DreamPreset &preset, QString *error)
{
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("dreamdsp-preset");
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("name")] = preset.name;

    // --- equalizer ---------------------------------------------------------
    {
        QJsonArray bands;
        for (const PresetBand &b : preset.eq.bands) {
            bands.append(QJsonObject{
                { QStringLiteral("type"), QString::fromLatin1(apoToken(b.type)) },
                { QStringLiteral("freq"), b.frequency },
                { QStringLiteral("gain"), b.gainDb },
                { QStringLiteral("q"), b.q },
                { QStringLiteral("enabled"), b.enabled },
            });
        }
        root[QStringLiteral("eq")] = QJsonObject{
            { QStringLiteral("enabled"), preset.eqEnabled },
            { QStringLiteral("preamp"), preset.eq.preamp },
            { QStringLiteral("bands"), bands },
        };
    }

    // --- effects -----------------------------------------------------------
    {
        const dsp::ParamBlock &p = preset.params;
        QJsonObject fx;
        // The mask is written as a number and also spelled out, so the file
        // says which effects are on without the reader having to know the bits.
        fx[QStringLiteral("enableMask")] = int(p.enableMask);
        fx[QStringLiteral("on")] = QJsonArray{};
        QJsonArray on;
        const struct { uint32_t bit; const char *name; } kNames[] = {
            { dsp::kEnBass, "bass" },          { dsp::kEnExciter, "exciter" },
            { dsp::kEnTube, "tube" },          { dsp::kEnComp, "compressor" },
            { dsp::kEnMultiband, "multiband" },{ dsp::kEnReverb, "reverb" },
            { dsp::kEnWidth, "width" },        { dsp::kEnCrossfeed, "crossfeed" },
            { dsp::kEnConvolution, "convolution" },
        };
        for (const auto &n : kNames) {
            if (p.enableMask & n.bit)
                on.append(QString::fromLatin1(n.name));
        }
        fx[QStringLiteral("on")] = on;

        fx[QStringLiteral("compressor")] = compressorToJson(p.comp);

        fx[QStringLiteral("reverb")] = QJsonObject{
            { QStringLiteral("roomSize"), p.reverb.roomSize },
            { QStringLiteral("damping"), p.reverb.damping },
            { QStringLiteral("density"), p.reverb.density },
            { QStringLiteral("bandwidth"), p.reverb.bandwidth },
            { QStringLiteral("preDelayMs"), p.reverb.preDelayMs },
            { QStringLiteral("width"), p.reverb.width },
            { QStringLiteral("wet"), p.reverb.wet },
            { QStringLiteral("dry"), p.reverb.dry },
        };
        fx[QStringLiteral("tube")] = QJsonObject{
            { QStringLiteral("drive"), p.tube.drive },
            { QStringLiteral("bias"), p.tube.bias },
            { QStringLiteral("mix"), p.tube.mix },
            { QStringLiteral("outputDb"), p.tube.outputDb },
        };
        fx[QStringLiteral("exciter")] = QJsonObject{
            { QStringLiteral("frequencyHz"), p.exciter.frequencyHz },
            { QStringLiteral("drive"), p.exciter.drive },
            { QStringLiteral("amount"), p.exciter.amount },
        };
        fx[QStringLiteral("bass")] = QJsonObject{
            { QStringLiteral("cutoffHz"), p.bass.cutoffHz },
            { QStringLiteral("amount"), p.bass.amount },
            { QStringLiteral("drive"), p.bass.drive },
            { QStringLiteral("removeOriginal"), p.bass.removeOriginal },
        };
        fx[QStringLiteral("width")] = QJsonObject{
            { QStringLiteral("width"), p.width.width },
            { QStringLiteral("monoBelowHz"), p.width.monoBelowHz },
        };
        fx[QStringLiteral("crossfeed")] = QJsonObject{
            { QStringLiteral("cutoffHz"), p.crossfeed.cutoffHz },
            { QStringLiteral("feedDb"), p.crossfeed.feedDb },
            { QStringLiteral("delayUs"), p.crossfeed.delayUs },
        };

        QJsonArray mbBands;
        QJsonArray mbGains;
        QJsonArray mbEnabled;
        for (int i = 0; i < dsp::MultibandCompressor::kBands; ++i) {
            mbBands.append(compressorToJson(p.multiband.band[i]));
            mbGains.append(p.multiband.bandGainDb[i]);
            mbEnabled.append(p.multiband.bandEnabled[i]);
        }
        fx[QStringLiteral("multiband")] = QJsonObject{
            { QStringLiteral("lowCrossHz"), p.multiband.lowCrossHz },
            { QStringLiteral("highCrossHz"), p.multiband.highCrossHz },
            { QStringLiteral("bands"), mbBands },
            { QStringLiteral("bandGainDb"), mbGains },
            { QStringLiteral("bandEnabled"), mbEnabled },
        };

        root[QStringLiteral("effects")] = fx;
    }

    // --- convolution -------------------------------------------------------
    root[QStringLiteral("convolution")] = QJsonObject{
        { QStringLiteral("file"), preset.convolutionFile },
        { QStringLiteral("name"), preset.convolutionName },
        { QStringLiteral("mix"), preset.params.convolution.mix },
        { QStringLiteral("trimDb"), preset.params.convolution.trimDb },
        { QStringLiteral("flags"), int(preset.params.convolution.flags) },
    };

    if (!preset.autoEqSource.isEmpty())
        root[QStringLiteral("autoEq")] = QJsonObject{ { QStringLiteral("source"), preset.autoEqSource } };

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool loadDreamPreset(const QString &path, DreamPreset *out, QString *error)
{
    if (!out)
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (doc.isNull() || !doc.isObject()) {
        if (error)
            *error = parseError.errorString();
        return false;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("dreamdsp-preset")) {
        if (error)
            *error = QStringLiteral("不是 DreamDSP 预设");
        return false;
    }

    *out = DreamPreset{};
    out->name = root.value(QStringLiteral("name")).toString();

    const QJsonObject eq = root.value(QStringLiteral("eq")).toObject();
    out->eqEnabled = eq.value(QStringLiteral("enabled")).toBool(true);
    out->eq.preamp = eq.value(QStringLiteral("preamp")).toDouble(0.0);
    for (const QJsonValue &v : eq.value(QStringLiteral("bands")).toArray()) {
        const QJsonObject b = v.toObject();
        PresetBand band;
        band.type = filterTypeFromName(b.value(QStringLiteral("type")).toString());
        band.frequency = b.value(QStringLiteral("freq")).toDouble(1000.0);
        band.gainDb = b.value(QStringLiteral("gain")).toDouble(0.0);
        band.q = b.value(QStringLiteral("q")).toDouble(1.41);
        band.enabled = b.value(QStringLiteral("enabled")).toBool(true);
        out->eq.bands.append(band);
    }

    dsp::ParamBlock &p = out->params;
    const QJsonObject fx = root.value(QStringLiteral("effects")).toObject();
    p.enableMask = uint32_t(fx.value(QStringLiteral("enableMask")).toInt(0));

    compressorFromJson(fx.value(QStringLiteral("compressor")).toObject(), &p.comp);

    const QJsonObject rv = fx.value(QStringLiteral("reverb")).toObject();
    p.reverb.roomSize = float(rv.value(QStringLiteral("roomSize")).toDouble(p.reverb.roomSize));
    p.reverb.damping = float(rv.value(QStringLiteral("damping")).toDouble(p.reverb.damping));
    p.reverb.density = float(rv.value(QStringLiteral("density")).toDouble(p.reverb.density));
    p.reverb.bandwidth = float(rv.value(QStringLiteral("bandwidth")).toDouble(p.reverb.bandwidth));
    p.reverb.preDelayMs = float(rv.value(QStringLiteral("preDelayMs")).toDouble(p.reverb.preDelayMs));
    p.reverb.width = float(rv.value(QStringLiteral("width")).toDouble(p.reverb.width));
    p.reverb.wet = float(rv.value(QStringLiteral("wet")).toDouble(p.reverb.wet));
    p.reverb.dry = float(rv.value(QStringLiteral("dry")).toDouble(p.reverb.dry));

    const QJsonObject tb = fx.value(QStringLiteral("tube")).toObject();
    p.tube.drive = float(tb.value(QStringLiteral("drive")).toDouble(p.tube.drive));
    p.tube.bias = float(tb.value(QStringLiteral("bias")).toDouble(p.tube.bias));
    p.tube.mix = float(tb.value(QStringLiteral("mix")).toDouble(p.tube.mix));
    p.tube.outputDb = float(tb.value(QStringLiteral("outputDb")).toDouble(p.tube.outputDb));

    const QJsonObject ex = fx.value(QStringLiteral("exciter")).toObject();
    p.exciter.frequencyHz = float(ex.value(QStringLiteral("frequencyHz")).toDouble(p.exciter.frequencyHz));
    p.exciter.drive = float(ex.value(QStringLiteral("drive")).toDouble(p.exciter.drive));
    p.exciter.amount = float(ex.value(QStringLiteral("amount")).toDouble(p.exciter.amount));

    const QJsonObject bs = fx.value(QStringLiteral("bass")).toObject();
    p.bass.cutoffHz = float(bs.value(QStringLiteral("cutoffHz")).toDouble(p.bass.cutoffHz));
    p.bass.amount = float(bs.value(QStringLiteral("amount")).toDouble(p.bass.amount));
    p.bass.drive = float(bs.value(QStringLiteral("drive")).toDouble(p.bass.drive));
    p.bass.removeOriginal = bs.value(QStringLiteral("removeOriginal")).toBool(p.bass.removeOriginal);

    const QJsonObject wd = fx.value(QStringLiteral("width")).toObject();
    p.width.width = float(wd.value(QStringLiteral("width")).toDouble(p.width.width));
    p.width.monoBelowHz = float(wd.value(QStringLiteral("monoBelowHz")).toDouble(p.width.monoBelowHz));

    const QJsonObject cf = fx.value(QStringLiteral("crossfeed")).toObject();
    p.crossfeed.cutoffHz = float(cf.value(QStringLiteral("cutoffHz")).toDouble(p.crossfeed.cutoffHz));
    p.crossfeed.feedDb = float(cf.value(QStringLiteral("feedDb")).toDouble(p.crossfeed.feedDb));
    p.crossfeed.delayUs = float(cf.value(QStringLiteral("delayUs")).toDouble(p.crossfeed.delayUs));

    const QJsonObject mb = fx.value(QStringLiteral("multiband")).toObject();
    p.multiband.lowCrossHz = float(mb.value(QStringLiteral("lowCrossHz")).toDouble(p.multiband.lowCrossHz));
    p.multiband.highCrossHz = float(mb.value(QStringLiteral("highCrossHz")).toDouble(p.multiband.highCrossHz));
    {
        const QJsonArray bands = mb.value(QStringLiteral("bands")).toArray();
        const QJsonArray gains = mb.value(QStringLiteral("bandGainDb")).toArray();
        const QJsonArray enabled = mb.value(QStringLiteral("bandEnabled")).toArray();
        for (int i = 0; i < dsp::MultibandCompressor::kBands; ++i) {
            if (i < bands.size())
                compressorFromJson(bands.at(i).toObject(), &p.multiband.band[i]);
            if (i < gains.size())
                p.multiband.bandGainDb[i] = float(gains.at(i).toDouble(0.0));
            if (i < enabled.size())
                p.multiband.bandEnabled[i] = enabled.at(i).toBool(true);
        }
    }

    const QJsonObject cv = root.value(QStringLiteral("convolution")).toObject();
    out->convolutionFile = cv.value(QStringLiteral("file")).toString();
    out->convolutionName = cv.value(QStringLiteral("name")).toString();
    p.convolution.mix = float(cv.value(QStringLiteral("mix")).toDouble(1.0));
    p.convolution.trimDb = float(cv.value(QStringLiteral("trimDb")).toDouble(0.0));
    p.convolution.flags = uint32_t(cv.value(QStringLiteral("flags")).toInt(0));

    out->autoEqSource = root.value(QStringLiteral("autoEq")).toObject()
                            .value(QStringLiteral("source")).toString();

    // Whatever a file claims, only values the DSP will accept come out.
    p = dsp::sanitise(p);
    return true;
}

} // namespace dreamdsp
