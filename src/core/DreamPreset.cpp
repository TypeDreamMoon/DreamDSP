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
            { dsp::kEnEqualizer, "equalizer" },
            { dsp::kEnMatrix, "matrix" },      { dsp::kEnDelay, "delay" },
            { dsp::kEnLoudness, "loudness" },
            { dsp::kEnGraphic, "graphic" }, { dsp::kEnDynBass, "dynBass" },
            { dsp::kEnLimiter, "limiter" }, { dsp::kEnTransient, "transient" },
            { dsp::kEnClipper, "clipper" }, { dsp::kEnAutoGain, "autoGain" },
            { dsp::kEnNightMode, "nightMode" },
            { dsp::kEnBinaural, "binaural" }, { dsp::kEnNeural, "neural" },
            { dsp::kEnChorus, "chorus" }, { dsp::kEnFlanger, "flanger" },
            { dsp::kEnPhaser, "phaser" }, { dsp::kEnFdn, "fdn" },
            { dsp::kEnTape, "tape" }, { dsp::kEnDeEsser, "deEsser" },
            { dsp::kEnGate, "gate" }, { dsp::kEnDynamicEq, "dynamicEq" },
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

        fx[QStringLiteral("binaural")] = QJsonObject{
            {QStringLiteral("speakerAngleDeg"), p.binaural.speakerAngleDeg},
            {QStringLiteral("yawDeg"), p.binaural.yawDeg},
            {QStringLiteral("mix"), p.binaural.mix},
            {QStringLiteral("outputDb"), p.binaural.outputDb},
            {QStringLiteral("elevationDeg"), p.binaural.elevationDeg},
            {QStringLiteral("model"), int(p.binaural.model)}
        };
        fx[QStringLiteral("neural")] = QJsonObject{
            {QStringLiteral("driveDb"), p.neural.driveDb},
            {QStringLiteral("mix"), p.neural.mix},
            {QStringLiteral("outputDb"), p.neural.outputDb},
            {QStringLiteral("model"), int(p.neural.model)}
        };
        fx[QStringLiteral("chorus")] = QJsonObject{
            {QStringLiteral("rateHz"), p.chorus.rateHz},
            {QStringLiteral("depth"), p.chorus.depth},
            {QStringLiteral("delayMs"), p.chorus.delayMs},
            {QStringLiteral("feedback"), p.chorus.feedback},
            {QStringLiteral("mix"), p.chorus.mix},
            {QStringLiteral("stereoPhase"), p.chorus.stereoPhase}
        };
        fx[QStringLiteral("flanger")] = QJsonObject{
            {QStringLiteral("rateHz"), p.flanger.rateHz},
            {QStringLiteral("depth"), p.flanger.depth},
            {QStringLiteral("delayMs"), p.flanger.delayMs},
            {QStringLiteral("feedback"), p.flanger.feedback},
            {QStringLiteral("mix"), p.flanger.mix},
            {QStringLiteral("stereoPhase"), p.flanger.stereoPhase}
        };
        fx[QStringLiteral("phaser")] = QJsonObject{
            {QStringLiteral("rateHz"), p.phaser.rateHz},
            {QStringLiteral("depth"), p.phaser.depth},
            {QStringLiteral("delayMs"), p.phaser.delayMs},
            {QStringLiteral("feedback"), p.phaser.feedback},
            {QStringLiteral("mix"), p.phaser.mix},
            {QStringLiteral("stereoPhase"), p.phaser.stereoPhase}
        };
        fx[QStringLiteral("fdn")] = QJsonObject{
            {QStringLiteral("decaySeconds"), p.fdn.decaySeconds},
            {QStringLiteral("dampingHz"), p.fdn.dampingHz},
            {QStringLiteral("preDelayMs"), p.fdn.preDelayMs},
            {QStringLiteral("mix"), p.fdn.mix},
            {QStringLiteral("width"), p.fdn.width},
            {QStringLiteral("outputDb"), p.fdn.outputDb}
        };
        fx[QStringLiteral("gate")] = QJsonObject{
            {QStringLiteral("thresholdDb"), p.gate.thresholdDb},
            {QStringLiteral("hysteresisDb"), p.gate.hysteresisDb},
            {QStringLiteral("rangeDb"), p.gate.rangeDb},
            {QStringLiteral("attackMs"), p.gate.attackMs},
            {QStringLiteral("holdMs"), p.gate.holdMs},
            {QStringLiteral("releaseMs"), p.gate.releaseMs}
        };
        QJsonArray dynamicBands;
        for (const auto &band : p.dynamicEq.band) {
            dynamicBands.append(QJsonObject{
                {QStringLiteral("frequencyHz"), band.frequencyHz},
                {QStringLiteral("q"), band.q},
                {QStringLiteral("thresholdDb"), band.thresholdDb},
                {QStringLiteral("ratio"), band.ratio},
                {QStringLiteral("maxReductionDb"), band.maxReductionDb},
                {QStringLiteral("attackMs"), band.attackMs},
                {QStringLiteral("releaseMs"), band.releaseMs},
                {QStringLiteral("enabled"), band.enabled != 0}
            });
        }
        fx[QStringLiteral("dynamicEq")] = dynamicBands;
        fx[QStringLiteral("tape")] = QJsonObject{
            {QStringLiteral("driveDb"), p.tape.driveDb},
            {QStringLiteral("toneHz"), p.tape.toneHz},
            {QStringLiteral("bumpDb"), p.tape.bumpDb},
            {QStringLiteral("memory"), p.tape.memory},
            {QStringLiteral("mix"), p.tape.mix},
            {QStringLiteral("outputDb"), p.tape.outputDb}
        };
        fx[QStringLiteral("deEsser")] = QJsonObject{
            {QStringLiteral("frequencyHz"), p.deEsser.frequencyHz},
            {QStringLiteral("thresholdDb"), p.deEsser.thresholdDb},
            {QStringLiteral("ratio"), p.deEsser.ratio},
            {QStringLiteral("maxReductionDb"), p.deEsser.maxReductionDb},
            {QStringLiteral("attackMs"), p.deEsser.attackMs},
            {QStringLiteral("releaseMs"), p.deEsser.releaseMs}
        };
        fx[QStringLiteral("transient")] = QJsonObject{
            {QStringLiteral("attack"), p.transient.attack},
            {QStringLiteral("sustain"), p.transient.sustain},
            {QStringLiteral("attackMs"), p.transient.attackMs},
            {QStringLiteral("releaseMs"), p.transient.releaseMs},
            {QStringLiteral("thresholdDb"), p.transient.thresholdDb}
        };
        fx[QStringLiteral("clipper")] = QJsonObject{
            {QStringLiteral("driveDb"), p.clipper.driveDb},
            {QStringLiteral("ceilingDb"), p.clipper.ceilingDb},
            {QStringLiteral("kneeDb"), p.clipper.kneeDb},
            {QStringLiteral("oversample"), p.clipper.oversample}
        };
        fx[QStringLiteral("autoGain")] = QJsonObject{
            {QStringLiteral("targetLufs"), p.autoGain.targetLufs},
            {QStringLiteral("maxGainDb"), p.autoGain.maxGainDb},
            {QStringLiteral("rateDbPerSec"), p.autoGain.rateDbPerSec},
            {QStringLiteral("windowDb"), p.autoGain.windowDb},
            {QStringLiteral("gateLufs"), p.autoGain.gateLufs}
        };
        fx[QStringLiteral("nightMode")] = QJsonObject{
            {QStringLiteral("profile"), int(p.nightMode.profile)},
            {QStringLiteral("boost"), p.nightMode.boost},
            {QStringLiteral("cut"), p.nightMode.cut},
            {QStringLiteral("referenceLufs"), p.nightMode.referenceLufs}
        };
        fx[QStringLiteral("limiter")] = QJsonObject{
            {QStringLiteral("gainDb"), p.limiter.gainDb},
            {QStringLiteral("thresholdDb"), p.limiter.thresholdDb},
            {QStringLiteral("releaseMs"), p.limiter.releaseMs},
            {QStringLiteral("lookaheadMs"), p.limiter.lookaheadMs},
            {QStringLiteral("truePeak"), p.limiter.truePeak}
        };
        fx[QStringLiteral("dynBass")] = QJsonObject{
            {QStringLiteral("maxGainDb"), p.dynBass.maxGainDb},
            {QStringLiteral("cutoffHz"), p.dynBass.cutoffHz},
            {QStringLiteral("releaseMs"), p.dynBass.releaseMs}
        };
        QJsonArray order;
        for (uint8_t id : p.order) order.append(int(id));
        fx[QStringLiteral("order")] = order;
        fx[QStringLiteral("flags")] = int(p.flags);
        fx[QStringLiteral("exciterThresholdDb")] = p.exciter.thresholdDb;
        fx[QStringLiteral("widthPhaseAmount")] = p.width.phaseAmount;
        QJsonArray graphic;
        for (float db : p.graphic.gainDb) graphic.append(db);
        fx[QStringLiteral("graphicGainDb")] = graphic;
        fx[QStringLiteral("graphicAmount")] = p.graphic.amount;
        root[QStringLiteral("effects")] = fx;
    }

    // --- output stage ------------------------------------------------------
    //
    // Written as named fields like everything else here rather than as the
    // block's bytes: a preset that survives a wire-format change is worth more
    // than one that is quick to serialise, and this file is meant to be
    // readable.
    {
        QJsonArray rows;
        for (int o = 0; o < dsp::ChannelMatrix::kMaxChannels; ++o) {
            QJsonArray row;
            for (int i = 0; i < dsp::ChannelMatrix::kMaxChannels; ++i)
                row.append(preset.params.matrix.gain[o][i]);
            rows.append(row);
        }
        QJsonArray delays;
        for (int c = 0; c < dsp::ChannelDelay::kMaxChannels; ++c)
            delays.append(preset.params.delay.ms[c]);

        root[QStringLiteral("output")] = QJsonObject{
            { QStringLiteral("matrix"), rows },
            { QStringLiteral("delayMs"), delays },
            { QStringLiteral("loudness"), QJsonObject{
                { QStringLiteral("referenceDb"), preset.params.loudness.referenceDb },
                { QStringLiteral("offsetDb"), preset.params.loudness.offsetDb },
                { QStringLiteral("amount"), preset.params.loudness.amount },
            } },
        };
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
    p.gate = dsp::NoiseGate::Params{};
    p.dynamicEq = dsp::DynamicEq::Params{};
    p.tape = dsp::TapeSaturation::Params{};
    p.deEsser = dsp::DeEsser::Params{};
    p.binaural = dsp::Binaural::Params{};
    p.neural = dsp::NeuralSaturation::Params{};
    p.chorus = dsp::Modulation::Params{};
    p.flanger = dsp::Modulation::Params{};
    p.phaser = dsp::Modulation::Params{};
    p.fdn = dsp::FdnReverb::Params{};
    p.flanger.delayMs = 2.0f;
    p.flanger.feedback = 0.5f;
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

    // --- output stage ------------------------------------------------------
    {
        const QJsonObject op = root.value(QStringLiteral("output")).toObject();
        // Absent for a preset written before the output stage existed. The
        // identity is the neutral matrix, not the zeroes a missing key would
        // otherwise leave, so it is set unconditionally and then overwritten.
        p.matrix = dsp::ChannelMatrix::identity();

        const QJsonArray rows = op.value(QStringLiteral("matrix")).toArray();
        for (int o = 0; o < rows.size() && o < dsp::ChannelMatrix::kMaxChannels; ++o) {
            const QJsonArray row = rows.at(o).toArray();
            for (int i = 0; i < row.size() && i < dsp::ChannelMatrix::kMaxChannels; ++i)
                p.matrix.gain[o][i] = float(row.at(i).toDouble());
        }

        const QJsonArray delays = op.value(QStringLiteral("delayMs")).toArray();
        for (int c = 0; c < delays.size() && c < dsp::ChannelDelay::kMaxChannels; ++c)
            p.delay.ms[c] = float(delays.at(c).toDouble());

        const QJsonObject ld = op.value(QStringLiteral("loudness")).toObject();
        p.loudness.referenceDb = float(ld.value(QStringLiteral("referenceDb")).toDouble(0.0));
        p.loudness.offsetDb = float(ld.value(QStringLiteral("offsetDb")).toDouble(0.0));
        p.loudness.amount = float(ld.value(QStringLiteral("amount")).toDouble(1.0));
    }

    {
        const QJsonObject o = fx.value(QStringLiteral("binaural")).toObject();
        p.binaural.speakerAngleDeg = float(o.value(QStringLiteral("speakerAngleDeg")).toDouble(p.binaural.speakerAngleDeg));
        p.binaural.yawDeg = float(o.value(QStringLiteral("yawDeg")).toDouble(p.binaural.yawDeg));
        p.binaural.mix = float(o.value(QStringLiteral("mix")).toDouble(p.binaural.mix));
        p.binaural.outputDb = float(o.value(QStringLiteral("outputDb")).toDouble(p.binaural.outputDb));
        p.binaural.elevationDeg = float(o.value(QStringLiteral("elevationDeg")).toDouble(0.0));
        p.binaural.model = uint32_t(o.value(QStringLiteral("model")).toInt(
            fx.contains(QStringLiteral("binaural")) ? 0 : 1));
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("neural")).toObject();
        p.neural.driveDb = float(o.value(QStringLiteral("driveDb")).toDouble(p.neural.driveDb));
        p.neural.mix = float(o.value(QStringLiteral("mix")).toDouble(p.neural.mix));
        p.neural.outputDb = float(o.value(QStringLiteral("outputDb")).toDouble(p.neural.outputDb));
        p.neural.model = uint32_t(o.value(QStringLiteral("model")).toInt(int(p.neural.model)));
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("chorus")).toObject();
        p.chorus.rateHz = float(o.value(QStringLiteral("rateHz")).toDouble(p.chorus.rateHz));
        p.chorus.depth = float(o.value(QStringLiteral("depth")).toDouble(p.chorus.depth));
        p.chorus.delayMs = float(o.value(QStringLiteral("delayMs")).toDouble(p.chorus.delayMs));
        p.chorus.feedback = float(o.value(QStringLiteral("feedback")).toDouble(p.chorus.feedback));
        p.chorus.mix = float(o.value(QStringLiteral("mix")).toDouble(p.chorus.mix));
        p.chorus.stereoPhase = float(o.value(QStringLiteral("stereoPhase")).toDouble(p.chorus.stereoPhase));
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("flanger")).toObject();
        p.flanger.rateHz = float(o.value(QStringLiteral("rateHz")).toDouble(p.flanger.rateHz));
        p.flanger.depth = float(o.value(QStringLiteral("depth")).toDouble(p.flanger.depth));
        p.flanger.delayMs = float(o.value(QStringLiteral("delayMs")).toDouble(p.flanger.delayMs));
        p.flanger.feedback = float(o.value(QStringLiteral("feedback")).toDouble(p.flanger.feedback));
        p.flanger.mix = float(o.value(QStringLiteral("mix")).toDouble(p.flanger.mix));
        p.flanger.stereoPhase = float(o.value(QStringLiteral("stereoPhase")).toDouble(p.flanger.stereoPhase));
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("phaser")).toObject();
        p.phaser.rateHz = float(o.value(QStringLiteral("rateHz")).toDouble(p.phaser.rateHz));
        p.phaser.depth = float(o.value(QStringLiteral("depth")).toDouble(p.phaser.depth));
        p.phaser.delayMs = float(o.value(QStringLiteral("delayMs")).toDouble(p.phaser.delayMs));
        p.phaser.feedback = float(o.value(QStringLiteral("feedback")).toDouble(p.phaser.feedback));
        p.phaser.mix = float(o.value(QStringLiteral("mix")).toDouble(p.phaser.mix));
        p.phaser.stereoPhase = float(o.value(QStringLiteral("stereoPhase")).toDouble(p.phaser.stereoPhase));
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("fdn")).toObject();
        p.fdn.decaySeconds = float(o.value(QStringLiteral("decaySeconds")).toDouble(p.fdn.decaySeconds));
        p.fdn.dampingHz = float(o.value(QStringLiteral("dampingHz")).toDouble(p.fdn.dampingHz));
        p.fdn.preDelayMs = float(o.value(QStringLiteral("preDelayMs")).toDouble(p.fdn.preDelayMs));
        p.fdn.mix = float(o.value(QStringLiteral("mix")).toDouble(p.fdn.mix));
        p.fdn.width = float(o.value(QStringLiteral("width")).toDouble(p.fdn.width));
        p.fdn.outputDb = float(o.value(QStringLiteral("outputDb")).toDouble(p.fdn.outputDb));
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("transient")).toObject();
        p.transient.attack = float(o.value(QStringLiteral("attack")).toDouble(p.transient.attack));
        p.transient.sustain = float(o.value(QStringLiteral("sustain")).toDouble(p.transient.sustain));
        p.transient.attackMs = float(o.value(QStringLiteral("attackMs")).toDouble(p.transient.attackMs));
        p.transient.releaseMs = float(o.value(QStringLiteral("releaseMs")).toDouble(p.transient.releaseMs));
        p.transient.thresholdDb = float(o.value(QStringLiteral("thresholdDb")).toDouble(p.transient.thresholdDb));
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("clipper")).toObject();
        p.clipper.driveDb = float(o.value(QStringLiteral("driveDb")).toDouble(p.clipper.driveDb));
        p.clipper.ceilingDb = float(o.value(QStringLiteral("ceilingDb")).toDouble(p.clipper.ceilingDb));
        p.clipper.kneeDb = float(o.value(QStringLiteral("kneeDb")).toDouble(p.clipper.kneeDb));
        p.clipper.oversample = o.value(QStringLiteral("oversample")).toBool(p.clipper.oversample);
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("autoGain")).toObject();
        p.autoGain.targetLufs = float(o.value(QStringLiteral("targetLufs")).toDouble(p.autoGain.targetLufs));
        p.autoGain.maxGainDb = float(o.value(QStringLiteral("maxGainDb")).toDouble(p.autoGain.maxGainDb));
        p.autoGain.rateDbPerSec = float(o.value(QStringLiteral("rateDbPerSec")).toDouble(p.autoGain.rateDbPerSec));
        p.autoGain.windowDb = float(o.value(QStringLiteral("windowDb")).toDouble(p.autoGain.windowDb));
        p.autoGain.gateLufs = float(o.value(QStringLiteral("gateLufs")).toDouble(p.autoGain.gateLufs));
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("nightMode")).toObject();
        p.nightMode.profile = o.value(QStringLiteral("profile")).toInt(p.nightMode.profile);
        p.nightMode.boost = float(o.value(QStringLiteral("boost")).toDouble(p.nightMode.boost));
        p.nightMode.cut = float(o.value(QStringLiteral("cut")).toDouble(p.nightMode.cut));
        p.nightMode.referenceLufs = float(o.value(QStringLiteral("referenceLufs")).toDouble(p.nightMode.referenceLufs));
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("limiter")).toObject();
        p.limiter.gainDb = float(o.value(QStringLiteral("gainDb")).toDouble(p.limiter.gainDb));
        p.limiter.thresholdDb = float(o.value(QStringLiteral("thresholdDb")).toDouble(p.limiter.thresholdDb));
        p.limiter.releaseMs = float(o.value(QStringLiteral("releaseMs")).toDouble(p.limiter.releaseMs));
        p.limiter.lookaheadMs = float(o.value(QStringLiteral("lookaheadMs")).toDouble(p.limiter.lookaheadMs));
        p.limiter.truePeak = o.value(QStringLiteral("truePeak")).toBool(p.limiter.truePeak);
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("dynBass")).toObject();
        p.dynBass.maxGainDb = float(o.value(QStringLiteral("maxGainDb")).toDouble(p.dynBass.maxGainDb));
        p.dynBass.cutoffHz = float(o.value(QStringLiteral("cutoffHz")).toDouble(p.dynBass.cutoffHz));
        p.dynBass.releaseMs = float(o.value(QStringLiteral("releaseMs")).toDouble(p.dynBass.releaseMs));
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("tape")).toObject();
        p.tape.driveDb = float(o.value(QStringLiteral("driveDb")).toDouble(p.tape.driveDb));
        p.tape.toneHz = float(o.value(QStringLiteral("toneHz")).toDouble(p.tape.toneHz));
        p.tape.bumpDb = float(o.value(QStringLiteral("bumpDb")).toDouble(p.tape.bumpDb));
        p.tape.memory = float(o.value(QStringLiteral("memory")).toDouble(p.tape.memory));
        p.tape.mix = float(o.value(QStringLiteral("mix")).toDouble(p.tape.mix));
        p.tape.outputDb = float(o.value(QStringLiteral("outputDb")).toDouble(p.tape.outputDb));
    }
    {
        const QJsonObject o = fx.value(QStringLiteral("deEsser")).toObject();
        p.deEsser.frequencyHz = float(o.value(QStringLiteral("frequencyHz")).toDouble(p.deEsser.frequencyHz));
        p.deEsser.thresholdDb = float(o.value(QStringLiteral("thresholdDb")).toDouble(p.deEsser.thresholdDb));
        p.deEsser.ratio = float(o.value(QStringLiteral("ratio")).toDouble(p.deEsser.ratio));
        p.deEsser.maxReductionDb = float(o.value(QStringLiteral("maxReductionDb")).toDouble(p.deEsser.maxReductionDb));
        p.deEsser.attackMs = float(o.value(QStringLiteral("attackMs")).toDouble(p.deEsser.attackMs));
        p.deEsser.releaseMs = float(o.value(QStringLiteral("releaseMs")).toDouble(p.deEsser.releaseMs));
    }
    {
        const auto o = fx.value(QStringLiteral("gate")).toObject();
        p.gate.thresholdDb = float(o.value(QStringLiteral("thresholdDb")).toDouble(p.gate.thresholdDb));
        p.gate.hysteresisDb = float(o.value(QStringLiteral("hysteresisDb")).toDouble(p.gate.hysteresisDb));
        p.gate.rangeDb = float(o.value(QStringLiteral("rangeDb")).toDouble(p.gate.rangeDb));
        p.gate.attackMs = float(o.value(QStringLiteral("attackMs")).toDouble(p.gate.attackMs));
        p.gate.holdMs = float(o.value(QStringLiteral("holdMs")).toDouble(p.gate.holdMs));
        p.gate.releaseMs = float(o.value(QStringLiteral("releaseMs")).toDouble(p.gate.releaseMs));
    }
    const auto dynamicBands = fx.value(QStringLiteral("dynamicEq")).toArray();
    for (int i = 0; i < dsp::DynamicEq::kBands && i < dynamicBands.size(); ++i) {
        const auto o = dynamicBands[i].toObject();
        auto &b = p.dynamicEq.band[i];
        b.frequencyHz = float(o.value(QStringLiteral("frequencyHz")).toDouble(b.frequencyHz));
        b.q = float(o.value(QStringLiteral("q")).toDouble(b.q));
        b.thresholdDb = float(o.value(QStringLiteral("thresholdDb")).toDouble(b.thresholdDb));
        b.ratio = float(o.value(QStringLiteral("ratio")).toDouble(b.ratio));
        b.maxReductionDb = float(o.value(QStringLiteral("maxReductionDb")).toDouble(b.maxReductionDb));
        b.attackMs = float(o.value(QStringLiteral("attackMs")).toDouble(b.attackMs));
        b.releaseMs = float(o.value(QStringLiteral("releaseMs")).toDouble(b.releaseMs));
        b.enabled = o.value(QStringLiteral("enabled")).toBool(b.enabled != 0) ? 1u : 0u;
    }
    p.flags = uint32_t(fx.value(QStringLiteral("flags")).toInt(0));
    p.exciter.thresholdDb = float(fx.value(QStringLiteral("exciterThresholdDb")).toDouble(p.exciter.thresholdDb));
    p.width.phaseAmount = float(fx.value(QStringLiteral("widthPhaseAmount")).toDouble(p.width.phaseAmount));
    const QJsonArray order = fx.value(QStringLiteral("order")).toArray();
    if (order.size() == 16 || order.size() == 20 || order.size() == 26 || order.size() == 28 || order.size() == dsp::kStageCount) {
        uint8_t ids[dsp::kStageCount]{};
        for (int i = 0; i < order.size(); ++i) {
            const int id = order[i].toInt(-1);
            ids[i] = id >= 0 && id < dsp::kStageCount ? uint8_t(id) : 0xFF;
        }
        dsp::upgradeStageOrder(ids, int(order.size()), p.order);
    }
    const QJsonArray graphic = fx.value(QStringLiteral("graphicGainDb")).toArray();
    for (int i = 0; i < graphic.size() && i < dsp::GraphicEq::kBands; ++i)
        p.graphic.gainDb[i] = float(graphic[i].toDouble());
    p.graphic.amount = float(fx.value(QStringLiteral("graphicAmount")).toDouble(p.graphic.amount));

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
