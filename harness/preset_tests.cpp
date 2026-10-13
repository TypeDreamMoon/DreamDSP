#include "core/DreamPreset.h"
#include "app/EffectsModel.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace dreamdsp;
    using namespace dreamdsp::dsp;
    int failures = 0;
    auto check = [&](bool ok, const char *name) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
        failures += !ok;
    };
    QDir().mkpath(QStringLiteral(DREAMDSP_TEST_DIR));
    QTemporaryDir dir(QStringLiteral(DREAMDSP_TEST_DIR "/preset-XXXXXX"));
    if (!dir.isValid()) return 1;
    const auto path = dir.filePath(QStringLiteral("roundtrip.dreamdsp"));
    DreamPreset input;
    input.name = QStringLiteral("DSP expansion");
    input.eq.preamp = -5.5;
    input.eq.bands.append({2300, -2.5, 1.3, FilterType::PK, true});
    auto &p = input.params;
    p.enableMask = kEnKnown;
    p.flags = kPfBypass;
    p.binaural = {55, -125, .8f, -4, 35, 1u};
    p.neural = {9, .6f, -3, 0};
    p.chorus = {.47f, .6f, 21, -.3f, .55f, .1f};
    p.flanger = {.9f, .7f, 3, .7f, .8f, .4f};
    p.phaser = {1.5f, .8f, 5, .2f, .9f, .6f};
    p.fdn = {3.2f, 4500, 45, .38f, 1.4f, -2};
    p.transient.attack = .6f;
    p.clipper.driveDb = 8;
    p.autoGain.targetLufs = -20;
    p.nightMode.boost = .4f;
    p.dynBass.maxGainDb = 7;
    p.limiter.thresholdDb = -2;
    p.exciter.thresholdDb = -55;
    p.width.phaseAmount = .3f;
    p.graphic.gainDb[4] = 2.2f;
    p.graphic.amount = .5f;
    std::reverse(std::begin(p.order), std::end(p.order));
    p = sanitise(p);
    QString error;
    DreamPreset output;
    check(saveDreamPreset(path, input, &error) && loadDreamPreset(path, &output, &error),
          "save and load a complete expanded preset");
    check(std::memcmp(&p, &output.params, sizeof p) == 0,
          "all DSP fields, enabled flags, bypass and custom order round-trip");
    check(output.eq.preamp == input.eq.preamp && output.eq.bands.size() == 1
          && output.eq.bands[0].frequency == 2300 && output.eq.bands[0].gainDb == -2.5,
          "equalizer settings round-trip");
    EffectsModel model;
    model.restore(output.params);
    const auto b = model.binauralParams();
    const auto n = model.neuralParams();
    check(std::memcmp(&b, &p.binaural, sizeof b) == 0
          && std::memcmp(&n, &p.neural, sizeof n) == 0
          && model.property("binauralEnabled").toBool()
          && model.property("neuralEnabled").toBool(), "Qt model restores spatial and neural controls");
    const auto c = model.chorusParams(), f = model.flangerParams(), ph = model.phaserParams();
    const auto fdn = model.fdnParams();
    check(std::memcmp(&c, &p.chorus, sizeof c) == 0
          && std::memcmp(&f, &p.flanger, sizeof f) == 0
          && std::memcmp(&ph, &p.phaser, sizeof ph) == 0
          && std::memcmp(&fdn, &p.fdn, sizeof fdn) == 0
          && model.chorusOn() && model.flangerOn() && model.phaserOn() && model.fdnOn(),
          "Qt model restores all modulation and FDN parameters and switches");
    model.setBinauralYaw(1000);
    model.setNeuralDrive(-1000);
    check(model.binauralYaw() == 180 && model.neuralDrive() == -12,
          "UI parameter limits match the DSP protocol");
    model.setBinauralYaw(std::numeric_limits<double>::quiet_NaN());
    check(model.binauralYaw() == 180, "UI rejects non-finite edits without corrupting controls");
    model.setBinauralSpread(60);
    model.setProperty("binauralRotation", 90);
    model.setBinauralElevation(45);
    check(model.binauralParams().speakerAngleDeg == 30
          && model.binauralParams().yawDeg == 90
          && model.binauralParams().elevationDeg == 45 && model.binauralModel() == 1,
          "head-fixed controls map full speaker spread, clockwise azimuth and elevation to the renderer");
    model.setBinauralModel(0);
    model.setBinauralElevation(-90);
    check(model.binauralModel() == 1 && model.binauralElevation() == -40,
          "height edits activate measured HRTFs and stop at the measured lower elevation");

    model.applyCinemaSpatialPreset();
    const auto cinemaChorus = model.chorusParams();
    check(model.binauralOn() && model.fdnOn() && !model.widthOn() && !model.crossfeedOn()
          && model.binauralModel() == 1 && model.binauralSpread() == 64
          && model.binauralElevation() == 8 && model.binauralYaw() == 0
          && model.neuralParams().driveDb == -12 && model.chorusOn()
          && std::memcmp(&c, &cinemaChorus, sizeof c) == 0,
          "cinema preset enables spatial room, removes overlapping stereo effects and preserves unrelated settings");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 1;
    auto root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    auto fx = root[QStringLiteral("effects")].toObject();
    auto oldSpatial = fx[QStringLiteral("binaural")].toObject();
    oldSpatial.remove(QStringLiteral("elevationDeg"));
    oldSpatial.remove(QStringLiteral("model"));
    fx[QStringLiteral("binaural")] = oldSpatial;
    root[QStringLiteral("effects")] = fx;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
    file.write(QJsonDocument(root).toJson());
    file.close();
    check(loadDreamPreset(path, &output, &error) && output.params.binaural.model == 0
          && output.params.binaural.elevationDeg == 0 && output.params.binaural.yawDeg == -125,
          "old JSON spatial presets retain the analytic model and their original direction");
    for (const char *key : {"binaural", "neural", "chorus", "flanger", "phaser", "fdn"})
        fx.remove(QLatin1String(key));
    fx[QStringLiteral("enableMask")] = int(kEnTube);
    QJsonArray oldOrder;
    for (int i = 19; i >= 0; --i) oldOrder.append(i);
    fx[QStringLiteral("order")] = oldOrder;
    root[QStringLiteral("effects")] = fx;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
    file.write(QJsonDocument(root).toJson());
    file.close();
    check(loadDreamPreset(path, &output, &error) && output.params.enableMask == kEnTube
          && output.params.binaural.mix == 1 && output.params.flanger.delayMs == 2,
          "legacy presets keep new effects disabled with usable defaults");
    int previous = 20;
    bool ordered = true;
    for (auto id : output.params.order) if (id < 20) { ordered &= id < previous; previous = id; }
    check(ordered, "legacy JSON order migration preserves existing relative order");
    return failures ? 1 : 0;
}
