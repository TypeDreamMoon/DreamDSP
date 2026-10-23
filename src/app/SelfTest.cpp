#include "app/SelfTest.h"

#include "app/AppController.h"
#include "app/PresetStore.h"
#include "platform/Autostart.h"
#include "core/ApoConfig.h"
#include "core/AutoEqDatabase.h"
#include "core/Biquad.h"
#include "core/Fft.h"
#include "GraphicEq.h"
#include "core/DreamPreset.h"
#include "core/FileUpdate.h"
#include "core/FiiEqXml.h"
#include "core/GraphicCurve.h"
#include "core/ParametricEqText.h"
#include "core/PeacePreset.h"
#include "platform/ApoInstaller.h"
#include "platform/ApoLocator.h"
#include "platform/AudioDevices.h"
#include "platform/UpdateChecker.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMouseEvent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cmath>

#include <windows.h>
#include <cstring>
#include <functional>

namespace dreamdsp {

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

int g_failures = 0;

void check(bool ok, const QString &what)
{
    out() << (ok ? "  ok   " : "  FAIL ") << what << Qt::endl;
    if (!ok)
        ++g_failures;
}

// ---------------------------------------------------------------- biquad

void testBiquad()
{
    out() << "\n[biquad]" << Qt::endl;

    // A peaking filter must hit its nominal gain at the centre frequency.
    for (const double gain : { -12.0, -3.0, 3.0, 12.0 }) {
        const auto c = designBiquad(FilterType::PK, 1000.0, gain, 1.41, 48000.0);
        const double got = magnitudeDb(c, 1000.0, 48000.0);
        check(std::abs(got - gain) < 0.05,
              QStringLiteral("PK 1 kHz %1 dB -> %2 dB at centre")
                  .arg(gain, 0, 'f', 1).arg(got, 0, 'f', 3));
    }

    // ...and be flat two decades away.
    const auto c = designBiquad(FilterType::PK, 1000.0, 12.0, 4.0, 48000.0);
    check(std::abs(magnitudeDb(c, 20.0, 48000.0)) < 0.2, QStringLiteral("PK is flat at 20 Hz"));

    // A low shelf approaches its full gain at DC and unity well above Fc.
    const auto ls = designBiquad(FilterType::LSC, 200.0, 6.0, 0.71, 48000.0);
    check(std::abs(magnitudeDb(ls, 20.0, 48000.0) - 6.0) < 0.6, QStringLiteral("LSC reaches gain at 20 Hz"));
    check(std::abs(magnitudeDb(ls, 10000.0, 48000.0)) < 0.2, QStringLiteral("LSC is flat at 10 kHz"));

    // A low-pass is -3 dB at its corner for Q = 1/sqrt(2).
    const auto lp = designBiquad(FilterType::LPQ, 1000.0, 0.0, 0.7071, 48000.0);
    check(std::abs(magnitudeDb(lp, 1000.0, 48000.0) + 3.0) < 0.2, QStringLiteral("LPQ is -3 dB at Fc"));

    // Token table must be self-consistent in both directions.
    bool allTokens = true;
    for (int i = 0; i < static_cast<int>(FilterType::Count); ++i) {
        const auto t = filterTypeFromIndex(i);
        if (filterTypeFromToken(QString::fromLatin1(apoToken(t))) != t)
            allTokens = false;
    }
    check(allTokens, QStringLiteral("all 18 filter tokens round-trip"));
    check(filterTypeFromIndex(14) == FilterType::LSCQ,
          QStringLiteral("Peace index 14 == LSCQ (matches Peace.au3:384)"));
}

// ------------------------------------------------------------- apo config

void testApoConfig()
{
    out() << "\n[apo config]" << Qt::endl;

    QStringList lines = { QStringLiteral("Preamp: -3 dB"),
                          QStringLiteral("Include: peace.txt"),
                          QStringLiteral("# a comment") };

    check(!ApoConfig::hasInclude(lines, QStringLiteral("dreamdsp.txt")),
          QStringLiteral("hasInclude is false before adding"));

    QStringList withUs = ApoConfig::withInclude(lines, QStringLiteral("dreamdsp.txt"));
    check(ApoConfig::hasInclude(withUs, QStringLiteral("dreamdsp.txt")),
          QStringLiteral("withInclude adds our line"));
    check(ApoConfig::hasInclude(withUs, QStringLiteral("peace.txt")),
          QStringLiteral("withInclude leaves Peace's line alone"));
    check(withUs.size() == lines.size() + 1, QStringLiteral("withInclude adds exactly one line"));

    // Adding twice must be a no-op, or repeated launches would pile up lines.
    check(ApoConfig::withInclude(withUs, QStringLiteral("dreamdsp.txt")).size() == withUs.size(),
          QStringLiteral("withInclude is idempotent"));

    const QStringList removed = ApoConfig::withoutInclude(withUs, QStringLiteral("dreamdsp.txt"));
    check(removed == lines, QStringLiteral("withoutInclude restores the original exactly"));

    // Case and spacing tolerance, mirroring APO's own trim-and-compare.
    check(ApoConfig::hasInclude({ QStringLiteral(" include :  DreamDSP.TXT ") },
                                QStringLiteral("dreamdsp.txt")),
          QStringLiteral("include matching ignores case and spacing"));

    // Written files must not carry a BOM: APO drops the first line if they do.
    QTemporaryDir tmp;
    if (tmp.isValid()) {
        const QString p = QDir(tmp.path()).filePath(QStringLiteral("t.txt"));
        ApoConfig::writeText(p, QStringLiteral("Preamp: 0 dB\r\n"));
        QFile f(p);
        f.open(QIODevice::ReadOnly);
        const QByteArray raw = f.readAll();
        check(!raw.startsWith("\xEF\xBB\xBF"), QStringLiteral("written config has no UTF-8 BOM"));
    }
}

// ---------------------------------------------------------------- presets

void testPresets(const ApoInstall &apo)
{
    out() << "\n[presets]" << Qt::endl;

    QStringList dirs;
    if (!apo.configPath.isEmpty())
        dirs << apo.configPath;
    dirs << PresetStore::userDirectory();

    int parsed = 0, failed = 0, roundTripped = 0;
    QTemporaryDir tmp;

    for (const QString &dirPath : std::as_const(dirs)) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;

        const QStringList files = dir.entryList({ QStringLiteral("*.peace") }, QDir::Files, QDir::Name);
        out() << "  scanning " << dirPath << " (" << files.size() << " files)" << Qt::endl;

        for (const QString &file : files) {
            Preset p;
            QString err;
            if (!PeaceFile::read(dir.filePath(file), &p, &err) || !p.isValid()) {
                out() << "  FAIL " << file << " -- " << err << Qt::endl;
                ++failed;
                ++g_failures;
                continue;
            }
            ++parsed;

            double minG = 0, maxG = 0;
            QStringList types;
            for (const PresetBand &b : std::as_const(p.bands)) {
                minG = std::min(minG, b.gainDb);
                maxG = std::max(maxG, b.gainDb);
                const QString t = QString::fromLatin1(apoToken(b.type));
                if (!types.contains(t))
                    types << t;
            }
            out() << QStringLiteral("    %1  %2 bands  gain %3..%4 dB  types: %5")
                         .arg(p.name, -34)
                         .arg(p.bands.size(), 2)
                         .arg(minG, 6, 'f', 1)
                         .arg(maxG, 5, 'f', 1)
                         .arg(types.join(QLatin1Char('/')))
                  << Qt::endl;

            // Write it back out and re-read: the band data must survive.
            if (tmp.isValid()) {
                const QString rt = QDir(tmp.path()).filePath(QStringLiteral("rt.peace"));
                Preset back;
                if (PeaceFile::write(rt, p) && PeaceFile::read(rt, &back)
                    && back.bands.size() == p.bands.size()) {
                    bool same = true;
                    for (int i = 0; i < p.bands.size(); ++i) {
                        const auto &a = p.bands[i];
                        const auto &b = back.bands[i];
                        if (std::abs(a.frequency - b.frequency) > 0.51   // written with 2 decimals
                            || std::abs(a.gainDb - b.gainDb) > 0.011
                            || std::abs(a.q - b.q) > 0.011
                            || a.type != b.type || a.enabled != b.enabled) {
                            same = false;
                            break;
                        }
                    }
                    if (same)
                        ++roundTripped;
                    else {
                        out() << "  FAIL round-trip mismatch: " << p.name << Qt::endl;
                        ++g_failures;
                    }
                }
            }
        }
    }

    check(parsed > 0, QStringLiteral("parsed %1 preset(s), %2 failed").arg(parsed).arg(failed));
    check(roundTripped == parsed,
          QStringLiteral("%1/%2 presets survive a write/read round-trip").arg(roundTripped).arg(parsed));
}

// ----------------------------------------------------------- environment

void testEnvironment(const ApoInstall &apo)
{
    out() << "\n[environment]" << Qt::endl;

    // Equalizer APO's absence is not a fault any more, so it is reported rather
    // than checked. Asserting it left a correctly configured machine showing two
    // failures, which is the fastest way to teach someone to stop reading the
    // output of a test suite.
    out() << (apo.found
                  ? QStringLiteral("  --   Equalizer APO %1 present (optional): %2")
                        .arg(apo.version, apo.configPath)
                  : QStringLiteral("  --   Equalizer APO not installed (fine -- not required)"))
          << Qt::endl;

    // DreamDSP's own component is what has to be right, so that is what gets
    // checked.
    const ApoState st = apoState();
    check(st.dllStaged, QStringLiteral("audio component staged at %1").arg(st.stagedDll));
    check(st.clsidRegistered, QStringLiteral("CLSID registered"));
    check(st.engineRegistered, QStringLiteral("registered with the audio engine"));
    if (st.dllStaged && !st.upToDate) {
        out() << "  --   staged copy is older than the one beside this build"
              << Qt::endl;
    }

    QString err;
    const auto devices = enumerateRenderDevices(&err);
    check(!devices.isEmpty(), QStringLiteral("enumerated %1 render endpoint(s)").arg(devices.size()));
    for (const AudioDevice &d : devices) {
        // The slot occupant and the endpoint's enhancement switch, side by side.
        // A device can hold our processing object and still be silent, and
        // without this line the two states are indistinguishable from here --
        // which is exactly how one went unnoticed for nineteen days.
        QString note;
        if (d.active) {
            const ApoSlot slot = apoSlotOf(d.id);
            if (slot.sysFxDisabled)
                note = QStringLiteral("  [enhancements OFF -- no APO will load]");
            else if (slot.isOurs)
                note = QStringLiteral("  [DreamDSP attached]");
            else if (!slot.clsid.isEmpty())
                note = QStringLiteral("  [slot: %1]").arg(slot.friendlyName.isEmpty()
                                                              ? slot.clsid : slot.friendlyName);
        }
        out() << QStringLiteral("    %1%2  %3%4")
                     .arg(d.isDefault ? QStringLiteral("* ") : QStringLiteral("  "))
                     .arg(d.name, -44)
                     .arg(d.active ? QStringLiteral("active") : QStringLiteral("inactive"))
                     .arg(note)
              << Qt::endl;
    }
}

void testFft()
{
    out() << "\n[fft]" << Qt::endl;

    constexpr int N = 2048;
    constexpr double sampleRate = 48000.0;
    const Fft fft(N);
    check(fft.size() == N, QStringLiteral("constructed a %1-point transform").arg(N));

    QVector<float> samples(N);
    QVector<float> mags;

    // A sine landing exactly on bin 100 must read back full amplitude there and
    // nothing either side -- this is what catches a wrong window gain or a
    // missing single-sided doubling.
    constexpr int kBin = 100;
    const double freq = kBin * sampleRate / N;
    for (int i = 0; i < N; ++i)
        samples[i] = float(std::sin(2.0 * 3.14159265358979323846 * freq * i / sampleRate));

    fft.magnitudeSpectrum(samples.constData(), &mags);
    check(mags.size() == N / 2 + 1, QStringLiteral("returns %1 bins").arg(mags.size()));
    check(std::abs(mags[kBin] - 1.0f) < 0.02f,
          QStringLiteral("full-scale sine reads %1 at bin %2 (want 1.0)")
              .arg(mags[kBin], 0, 'f', 4).arg(kBin));

    int peakBin = 0;
    for (int i = 1; i < mags.size(); ++i)
        if (mags[i] > mags[peakBin]) peakBin = i;
    check(peakBin == kBin, QStringLiteral("peak lands in bin %1").arg(peakBin));

    float away = 0.0f;
    for (int i = 0; i < mags.size(); ++i)
        if (std::abs(i - kBin) > 3) away = std::max(away, mags[i]);
    check(away < 0.01f, QStringLiteral("leakage away from the peak is %1").arg(away, 0, 'f', 5));

    // Half amplitude must read half, not a quarter.
    for (int i = 0; i < N; ++i)
        samples[i] *= 0.5f;
    fft.magnitudeSpectrum(samples.constData(), &mags);
    check(std::abs(mags[kBin] - 0.5f) < 0.02f,
          QStringLiteral("half-scale sine reads %1 (want 0.5)").arg(mags[kBin], 0, 'f', 4));

    // Silence must not produce anything.
    samples.fill(0.0f);
    fft.magnitudeSpectrum(samples.constData(), &mags);
    float loudest = 0.0f;
    for (float m : mags) loudest = std::max(loudest, m);
    check(loudest < 1e-6f, QStringLiteral("silence reads %1").arg(loudest, 0, 'e', 2));
}

void testAutoEq(const ApoInstall &apo)
{
    out() << "\n[autoeq]" << Qt::endl;

    AutoEqDatabase db;
    QString err;
    if (!db.load({ PresetStore::userDirectory() + QStringLiteral("/autoeq"),
                   PresetStore::userDirectory(), apo.configPath }, &err)) {
        // Absent databases are missing optional data, not a fault. They ship
        // with Peace and Equalizer APO, and uninstalling either takes the path
        // DreamDSP used to find them with -- which is a thing to say, not a
        // thing to fail on. Copy them into <presets>/autoeq to keep them.
        out() << QStringLiteral("  --   AutoEQ databases not present: %1").arg(err) << Qt::endl;
        return;
    }
    check(db.entries().size() > 1000,
          QStringLiteral("loaded %1 entries").arg(db.entries().size()));

    // Per-source tally, so a database silently failing to parse is visible.
    const QStringList names = AutoEqDatabase::sourceNames();
    QVector<int> perSource(names.size(), 0);
    int withFixed = 0;
    for (const AutoEqEntry &e : db.entries()) {
        if (e.source >= 0 && e.source < perSource.size())
            ++perSource[e.source];
        if (!e.fixedBand.isEmpty())
            ++withFixed;
    }
    for (int i = 0; i < names.size(); ++i)
        out() << QStringLiteral("    %1  %2").arg(names.at(i), -10).arg(perSource.at(i)) << Qt::endl;
    out() << QStringLiteral("    with fixed-band variant: %1").arg(withFixed) << Qt::endl;

    // Convert everything and check the numbers are physically sensible. A
    // mis-parse shows up here as absurd frequencies or gains.
    int converted = 0, badFreq = 0, badGain = 0, badQ = 0, failed = 0;
    for (const AutoEqEntry &e : db.entries()) {
        Preset p;
        if (!AutoEqDatabase::toPreset(e, false, &p)) {
            ++failed;
            continue;
        }
        ++converted;
        for (const PresetBand &b : p.bands) {
            // Lower bound is 1 Hz, not 10: oratory1990 legitimately publishes
            // filters as low as 8.5 Hz (Ultrasone Edition 15).
            const bool fBad = (b.frequency < 1.0 || b.frequency > 24000.0);
            const bool gBad = (b.gainDb < -40.0 || b.gainDb > 40.0);
            const bool qBad = (b.q <= 0.0 || b.q > 100.0);
            if (fBad) ++badFreq;
            if (gBad) ++badGain;
            if (qBad) ++badQ;
            if (fBad || gBad || qBad) {
                out() << QStringLiteral("    suspect: %1").arg(e.displayName()) << Qt::endl;
                out() << QStringLiteral("      raw : %1").arg(e.parametric) << Qt::endl;
                out() << QStringLiteral("      band: %1 Fc %2 Gain %3 Q %4")
                             .arg(QString::fromLatin1(apoToken(b.type)))
                             .arg(b.frequency).arg(b.gainDb).arg(b.q)
                      << Qt::endl;
            }
        }
    }
    check(failed == 0, QStringLiteral("all %1 parametric entries convert (%2 failed)")
                           .arg(converted).arg(failed));
    check(badFreq == 0, QStringLiteral("all frequencies within 1 Hz .. 24 kHz (%1 bad)").arg(badFreq));
    check(badGain == 0, QStringLiteral("all gains within +-40 dB (%1 bad)").arg(badGain));
    check(badQ == 0, QStringLiteral("all Q values within 0 .. 100 (%1 bad)").arg(badQ));

    // Fixed-band variants must land exactly on the ten documented frequencies.
    int fixedChecked = 0, fixedBad = 0;
    static const double kFixed[] = { 31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    for (const AutoEqEntry &e : db.entries()) {
        if (e.fixedBand.isEmpty())
            continue;
        Preset p;
        if (!AutoEqDatabase::toPreset(e, true, &p))
            continue;
        ++fixedChecked;
        for (int i = 0; i < p.bands.size() && i < 10; ++i) {
            if (!qFuzzyCompare(p.bands[i].frequency, kFixed[i]))
                ++fixedBad;
        }
        if (fixedChecked >= 500)
            break;
    }
    check(fixedBad == 0, QStringLiteral("fixed-band frequencies match the table (%1 checked)")
                             .arg(fixedChecked));

    // Show one worked example so the mapping can be eyeballed.
    for (const AutoEqEntry &e : db.entries()) {
        Preset p;
        if (e.parametric.isEmpty() || !AutoEqDatabase::toPreset(e, false, &p) || p.bands.size() < 5)
            continue;
        out() << "  example: " << e.displayName() << Qt::endl;
        out() << "    raw: " << e.parametric.left(96) << Qt::endl;
        out() << QStringLiteral("    -> preamp %1 dB, %2 bands").arg(p.preamp, 0, 'f', 1).arg(p.bands.size())
              << Qt::endl;
        for (int i = 0; i < std::min<qsizetype>(4, p.bands.size()); ++i) {
            const PresetBand &b = p.bands[i];
            out() << QStringLiteral("       %1 Fc %2 Hz  Gain %3 dB  Q %4")
                         .arg(QString::fromLatin1(apoToken(b.type)), -4)
                         .arg(b.frequency, 6, 'f', 0).arg(b.gainDb, 5, 'f', 1).arg(b.q, 5, 'f', 2)
                  << Qt::endl;
        }
        break;
    }
}

// A preset that silently dropped half of what it claims to save would be worse
// than not offering one, so what comes back is compared field by field against
// what went in -- not merely "it parsed".
void testFullPreset()
{
    out() << "\n[full preset]" << Qt::endl;

    QTemporaryDir dir;
    if (!dir.isValid()) {
        check(false, QStringLiteral("temporary directory"));
        return;
    }
    const QString path = dir.filePath(QStringLiteral("test.dreamdsp"));

    DreamPreset in;
    in.name = QStringLiteral("测试预设");
    in.eqEnabled = false;
    in.eq.preamp = -6.5;
    in.eq.bands = {
        { 105.0, 5.5, 0.70, FilterType::LSC, true },
        { 3842.0, -3.3, 3.05, FilterType::PK, false },
        { 10000.0, 1.25, 0.71, FilterType::HSQ, true },
    };

    in.params = dsp::transparentBlock();
    in.params.enableMask = dsp::kEnComp | dsp::kEnTube | dsp::kEnConvolution;
    in.params.comp.thresholdDb = -18.5f;
    in.params.comp.ratio = 3.5f;
    in.params.comp.autoMakeup = true;
    in.params.reverb.wet = 0.42f;
    in.params.tube.drive = 4.25f;
    in.params.tube.bias = 0.35f;
    in.params.exciter.frequencyHz = 5500.0f;
    in.params.bass.removeOriginal = true;
    in.params.width.width = 1.4f;
    in.params.crossfeed.feedDb = -7.5f;
    in.params.multiband.lowCrossHz = 180.0f;
    in.params.multiband.band[1].ratio = 2.5f;
    in.params.multiband.bandGainDb[2] = -3.5f;
    in.params.multiband.bandEnabled[1] = false;
    in.params.convolution.mix = 0.75f;
    in.params.convolution.trimDb = -2.5f;
    // The output stage, so the byte comparison below covers it too. A channel
    // swap is deliberate: it is the one matrix whose loss would be inaudible
    // until the moment it matters.
    in.params.enableMask |= dsp::kEnMatrix | dsp::kEnDelay | dsp::kEnLoudness;
    in.params.matrix.gain[0][0] = 0.0f;
    in.params.matrix.gain[0][1] = 1.0f;
    in.params.matrix.gain[1][0] = 1.0f;
    in.params.matrix.gain[1][1] = 0.0f;
    in.params.delay.ms[1] = 3.75f;
    in.params.delay.ms[4] = 12.5f;
    in.params.loudness.referenceDb = -22.5f;
    in.params.loudness.offsetDb = 2.5f;
    in.params.loudness.amount = 0.625f;
    in.params = dsp::sanitise(in.params);

    in.convolutionFile = QStringLiteral("C:/somewhere/room.wav");
    in.convolutionName = QStringLiteral("room");
    in.autoEqSource = QStringLiteral("1Custom SA02 · crinacle");

    QString err;
    if (!saveDreamPreset(path, in, &err)) {
        check(false, QStringLiteral("save: %1").arg(err));
        return;
    }
    check(true, QStringLiteral("saved"));

    DreamPreset back;
    if (!loadDreamPreset(path, &back, &err)) {
        check(false, QStringLiteral("load: %1").arg(err));
        return;
    }

    check(back.name == in.name, QStringLiteral("name survives"));
    check(back.eqEnabled == in.eqEnabled, QStringLiteral("equalizer enable survives"));
    check(std::abs(back.eq.preamp - in.eq.preamp) < 1e-9, QStringLiteral("preamp survives"));
    check(back.eq.bands.size() == in.eq.bands.size(), QStringLiteral("band count survives"));

    bool bandsMatch = back.eq.bands.size() == in.eq.bands.size();
    for (int i = 0; bandsMatch && i < in.eq.bands.size(); ++i) {
        const PresetBand &a = in.eq.bands[i];
        const PresetBand &b = back.eq.bands[i];
        bandsMatch = a.type == b.type && a.enabled == b.enabled
                     && std::abs(a.frequency - b.frequency) < 1e-6
                     && std::abs(a.gainDb - b.gainDb) < 1e-6
                     && std::abs(a.q - b.q) < 1e-6;
    }
    check(bandsMatch, QStringLiteral("every band survives, type and enable included"));

    // The whole effect block, compared as bytes. Anything the serialiser
    // forgets shows up here rather than as a field nobody thought to check.
    check(std::memcmp(&in.params, &back.params, sizeof(dsp::ParamBlock)) == 0,
          QStringLiteral("the effect block round-trips byte for byte"));

    check(back.convolutionFile == in.convolutionFile
              && back.convolutionName == in.convolutionName,
          QStringLiteral("the impulse response reference survives"));
    check(back.autoEqSource == in.autoEqSource, QStringLiteral("the AutoEQ source survives"));

    // A file that is not one of ours has to be refused rather than
    // half-interpreted.
    {
        const QString junk = dir.filePath(QStringLiteral("junk.dreamdsp"));
        QFile f(junk);
        f.open(QIODevice::WriteOnly);
        f.write("{\"format\":\"something-else\"}");
        f.close();
        DreamPreset ignored;
        check(!loadDreamPreset(junk, &ignored, nullptr),
              QStringLiteral("a foreign file is refused"));
    }
}

// --------------------------------------------------------- graphic curves

void testGraphicCurve()
{
    out() << "\n[graphic curve]" << Qt::endl;

    // AutoEQ's own spelling.
    {
        const GraphicCurve c = parseGraphicCurve(
            QStringLiteral("GraphicEQ: 20 -1.5; 25 -1.4; 31.5 -1.3; 20000 -8.0"));
        check(c.size() == 4, QStringLiteral("the GraphicEQ line parses to 4 points"));
        check(c.size() == 4 && qFuzzyCompare(c.first().first, 20.0)
                  && qFuzzyCompare(c.first().second, -1.5),
              QStringLiteral("first point is 20 Hz, -1.5 dB"));
        check(c.size() == 4 && qFuzzyCompare(c.last().first, 20000.0),
              QStringLiteral("last point is 20 kHz"));
    }

    // The two-column form, with a comment, a blank line, junk, and the points
    // out of order -- all of which turn up in hand-edited files.
    {
        const GraphicCurve c = parseGraphicCurve(QStringLiteral(
            "# my curve\n1000 0\n\n100 3.5\nnot a line\n10000 -2\n"));
        check(c.size() == 3, QStringLiteral("the two-column form parses, junk skipped"));
        check(c.size() == 3 && c.at(0).first < c.at(1).first && c.at(1).first < c.at(2).first,
              QStringLiteral("and comes back sorted by frequency"));
    }

    // A repeated frequency would make the log interpolation divide by zero.
    {
        const GraphicCurve c = parseGraphicCurve(QStringLiteral("100 1\n100 5\n1000 0"));
        check(c.size() == 2, QStringLiteral("a repeated frequency is collapsed"));
        check(c.size() == 2 && qFuzzyCompare(c.first().second, 5.0),
              QStringLiteral("and the later value wins"));
    }

    // Interpolation is in log frequency: halfway between 100 and 1000 Hz is
    // 316 Hz, not 550, and the value there has to be halfway too.
    {
        const GraphicCurve c = parseGraphicCurve(QStringLiteral("100 0\n1000 10"));
        const double f[3] = { 50.0, 316.227766, 5000.0 };
        double g[3] = { 0, 0, 0 };
        resampleCurve(c, f, 3, g);
        check(std::abs(g[1] - 5.0) < 1e-6,
              QStringLiteral("316 Hz is the midpoint on a log axis (%1 dB)").arg(g[1]));
        check(std::abs(g[0] - 0.0) < 1e-9 && std::abs(g[2] - 10.0) < 1e-9,
              QStringLiteral("outside the data the curve is held flat, not extrapolated"));
    }

    // End to end: a curve in, band gains out, and the cascade those gains
    // describe measured back against what was asked for.
    {
        QString text = QStringLiteral("GraphicEQ:");
        for (int i = 0; i < 60; ++i) {
            const double hz = 20.0 * std::pow(1000.0, double(i) / 59.0);
            const double db = 5.0 * std::sin(std::log(hz / 20.0) * 1.1) - 2.0;
            text += QStringLiteral(" %1 %2;").arg(hz, 0, 'f', 2).arg(db, 0, 'f', 2);
        }
        const GraphicCurve c = parseGraphicCurve(text);
        check(c.size() == 60, QStringLiteral("a 60-point curve parses"));

        double target[dsp::GraphicEq::kBands];
        resampleCurve(c, dsp::GraphicEq::centres(), dsp::GraphicEq::kBands, target);

        float gains[dsp::GraphicEq::kBands];
        const double residual = dsp::fitGraphicEq(target, 48000.0, gains);
        check(residual < 0.3,
              QStringLiteral("the 31-band fit lands within %1 dB of it")
                  .arg(residual, 0, 'f', 3));
    }
}

// Getting this wrong strands every installed copy: a version comparison that
// says 0.10.0 is older than 0.9.0 means the update is never offered again, and
// nothing about the application looks broken while it happens.
void testVersionCompare()
{
    out() << "\n[version compare]" << Qt::endl;

    const auto cmp = &UpdateChecker::compareVersions;

    check(cmp(QStringLiteral("1.0.0"), QStringLiteral("1.0.0")) == 0, QStringLiteral("equal"));
    check(cmp(QStringLiteral("1.0.0"), QStringLiteral("1.0.1")) < 0, QStringLiteral("patch"));
    check(cmp(QStringLiteral("1.0.0"), QStringLiteral("1.1.0")) < 0, QStringLiteral("minor"));
    check(cmp(QStringLiteral("1.0.0"), QStringLiteral("2.0.0")) < 0, QStringLiteral("major"));

    // The one a string comparison gets backwards.
    check(cmp(QStringLiteral("0.9.0"), QStringLiteral("0.10.0")) < 0,
          QStringLiteral("0.9.0 is older than 0.10.0"));
    check(cmp(QStringLiteral("0.2.0"), QStringLiteral("0.10.0")) < 0,
          QStringLiteral("0.2.0 is older than 0.10.0"));

    // A development build carries a suffix; it must compare as its release.
    check(cmp(QStringLiteral("0.2.0+7.abc1234"), QStringLiteral("0.2.0")) == 0,
          QStringLiteral("a build suffix does not change the ordering"));
    check(cmp(QStringLiteral("0.2.0+7.abc1234-dirty"), QStringLiteral("0.3.0")) < 0,
          QStringLiteral("a dirty build still sees a newer release"));

    // Short and malformed forms must not crash or invent an ordering.
    check(cmp(QStringLiteral("1"), QStringLiteral("1.0.0")) == 0,
          QStringLiteral("missing components read as zero"));
    check(cmp(QStringLiteral(""), QStringLiteral("0.0.1")) < 0,
          QStringLiteral("an empty version is older than anything"));
}

void testAutostart()
{
    out() << "\n[autostart]" << Qt::endl;

    const QString cmd = autostart::command();
    out() << "  command: " << cmd << Qt::endl;
    check(cmd.startsWith(QLatin1Char('"')) && cmd.contains(QLatin1String("DreamDSP.exe\"")),
          QStringLiteral("executable path is quoted"));
    check(cmd.endsWith(QLatin1String("--tray")),
          QStringLiteral("registered command starts hidden"));

    const bool wasEnabled = autostart::isEnabled();
    out() << "  currently " << (wasEnabled ? "enabled" : "disabled") << Qt::endl;

    if (wasEnabled) {
        // Something is registered. Toggling it off and on again would be a real
        // side effect on the user's login, so stop at reporting.
        out() << "  (skipping write round-trip: an entry already exists)" << Qt::endl;
        return;
    }

    QString err;
    if (!autostart::setEnabled(true, &err)) {
        check(false, QStringLiteral("enable failed: %1").arg(err));
        return;
    }
    check(autostart::isEnabled(), QStringLiteral("enable registers the Run value"));

    if (!autostart::setEnabled(false, &err)) {
        check(false, QStringLiteral("disable failed: %1").arg(err));
        return;
    }
    check(!autostart::isEnabled(), QStringLiteral("disable removes it again"));
}

} // namespace

int runSliderTest(QObject *rootObject)
{
    g_failures = 0;
    out() << "DreamDSP slider test" << Qt::endl;

    auto *window = qobject_cast<QQuickWindow *>(rootObject);
    if (!window) {
        check(false, QStringLiteral("root object is a QQuickWindow"));
        out().flush();
        return g_failures;
    }

    // HusSlider tags itself with this objectName; the first one in the scene is
    // the preamp, the second is equalizer band 0.
    // Walk the visual tree, not QObject::findChildren: Repeater delegates are
    // not reliably QObject children of the window, and the visual tree is what
    // hit-testing actually uses anyway.
    QList<QQuickItem *> all;
    std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        for (QQuickItem *child : item->childItems()) {
            all.append(child);
            walk(child);
        }
    };
    walk(window->contentItem());

    // Match on the metaobject rather than objectName: QML type names survive
    // regardless of what a delegate does to objectName.
    const auto typeName = [](const QQuickItem *item) {
        QString cls = QString::fromLatin1(item->metaObject()->className());
        const int cut = cls.indexOf(QLatin1String("_QML"));
        return cut > 0 ? cls.left(cut) : cls;
    };

    // Identify sliders by their interface rather than by type name: they are
    // wrapped in Fader.qml, and any future wrapper would silently stop being
    // tested if this matched on the class name.
    const auto isSlider = [](const QQuickItem *item) {
        const QMetaObject *mo = item->metaObject();
        return mo->indexOfProperty("currentValue") >= 0
               && mo->indexOfProperty("orientation") >= 0
               && mo->indexOfProperty("min") >= 0
               && mo->indexOfProperty("max") >= 0;
    };

    QList<QQuickItem *> sliders;
    for (QQuickItem *item : all) {
        if (isSlider(item))
            sliders.append(item);
    }

    out() << "  scanned " << all.size() << " QQuickItem(s)" << Qt::endl;
    out() << "  HusSlider instances: " << sliders.size()
          << "  (page " << rootObject->property("page").toInt() << ")" << Qt::endl;
    for (int i = 0; i < sliders.size() && i < 20; ++i) {
        QQuickItem *s = sliders.at(i);
        const QPointF at = s->mapToScene(QPointF(0, 0));
        out() << QStringLiteral("    [%1] %2x%3 at %4,%5 visible=%6 enabled=%7 opacity=%8 parent=%9")
                     .arg(i, 2).arg(s->width(), 6, 'f', 1).arg(s->height(), 6, 'f', 1)
                     .arg(at.x(), 6, 'f', 1).arg(at.y(), 6, 'f', 1)
                     .arg(s->isVisible()).arg(s->isEnabled()).arg(s->opacity(), 0, 'f', 2)
                     .arg(s->parentItem() ? typeName(s->parentItem()) : QStringLiteral("<none>"))
              << Qt::endl;
    }

    // A visible slider with a zero dimension is the recurring HuskarUI trap:
    // it renders perfectly and receives no mouse events, because the track and
    // handle draw outside a parent whose bounds are empty. Catch it here rather
    // than waiting for someone to report that a control does not respond.
    int zeroSized = 0;
    for (QQuickItem *s : std::as_const(sliders)) {
        if (s->isVisible() && (s->width() <= 0.0 || s->height() <= 0.0))
            ++zeroSized;
    }
    check(zeroSized == 0,
          QStringLiteral("no visible slider has a zero dimension (%1 offending)").arg(zeroSized));

    // Keep only ones that can actually be clicked.
    sliders.erase(std::remove_if(sliders.begin(), sliders.end(), [](QQuickItem *s) {
                      return !s->isVisible() || !s->isEnabled()
                             || s->width() <= 0 || s->height() <= 0;
                  }),
                  sliders.end());
    out() << "  usable: " << sliders.size() << Qt::endl;

    // The drag assertions below read the equalizer model, so they only apply
    // on that page. Elsewhere the geometry check above is the whole test.
    if (rootObject->property("page").toInt() != 0) {
        out() << "  (not on the equalizer page; geometry only)" << Qt::endl;
        out() << "\n" << (g_failures == 0 ? "PASS" : "FAIL")
              << " -- " << g_failures << " failure(s)" << Qt::endl;
        out().flush();
        return g_failures;
    }
    if (sliders.size() < 2) {
        check(false, QStringLiteral("at least 2 sliders present (preamp + bands)"));
        out().flush();
        return g_failures;
    }

    // Sort left-to-right so index 0 is the preamp and index 1 is band 0.
    std::sort(sliders.begin(), sliders.end(), [](QQuickItem *a, QQuickItem *b) {
        return a->mapToScene(QPointF(0, 0)).x() < b->mapToScene(QPointF(0, 0)).x();
    });

    QQuickItem *band0 = sliders.at(1);
    out() << QStringLiteral("  band 0 slider: %1 x %2 at scene %3,%4")
                 .arg(band0->width()).arg(band0->height())
                 .arg(band0->mapToScene(QPointF(0, 0)).x())
                 .arg(band0->mapToScene(QPointF(0, 0)).y())
          << Qt::endl;

    // The singleton is owned by the engine, not parented into the scene graph.
    AppController *ctl = nullptr;
    if (auto *engine = qmlEngine(rootObject))
        ctl = engine->singletonInstance<AppController *>("DreamDSP", "AppController");
    if (!ctl) {
        check(false, QStringLiteral("AppController singleton reachable"));
        out().flush();
        return g_failures;
    }

    check(ctl->trayActive(),
          QStringLiteral("tray icon installed (Shell_NotifyIcon on the main window)"));

    const double before = ctl->bands()->gain(0);

    // Drag from 70% down the track up to 25% -- for a vertical slider that is a
    // clear increase, well away from both ends.
    const QPointF start = band0->mapToScene(QPointF(band0->width() / 2.0, band0->height() * 0.70));
    const QPointF mid   = band0->mapToScene(QPointF(band0->width() / 2.0, band0->height() * 0.45));
    const QPointF end   = band0->mapToScene(QPointF(band0->width() / 2.0, band0->height() * 0.25));

    const auto post = [window](QEvent::Type type, const QPointF &pos, Qt::MouseButton button) {
        QMouseEvent ev(type, pos, window->mapToGlobal(pos.toPoint()),
                       button, (type == QEvent::MouseButtonRelease) ? Qt::NoButton : Qt::LeftButton,
                       Qt::NoModifier);
        QCoreApplication::sendEvent(window, &ev);
    };

    post(QEvent::MouseButtonPress, start, Qt::LeftButton);
    const double afterPress = ctl->bands()->gain(0);
    post(QEvent::MouseMove, mid, Qt::NoButton);
    const double afterMid = ctl->bands()->gain(0);
    post(QEvent::MouseMove, end, Qt::NoButton);
    const double afterMove = ctl->bands()->gain(0);
    post(QEvent::MouseButtonRelease, end, Qt::LeftButton);
    const double afterRelease = ctl->bands()->gain(0);

    out() << QStringLiteral("  gain: before=%1 press=%2 mid=%3 move=%4 release=%5")
                 .arg(before, 0, 'f', 2).arg(afterPress, 0, 'f', 2)
                 .arg(afterMid, 0, 'f', 2).arg(afterMove, 0, 'f', 2)
                 .arg(afterRelease, 0, 'f', 2)
          << Qt::endl;

    check(std::abs(afterPress - before) > 0.001,
          QStringLiteral("press alone moves the value (slider accepts input at all)"));
    check(std::abs(afterMove - afterPress) > 0.001,
          QStringLiteral("dragging further changes the value"));
    check(afterMove > afterPress,
          QStringLiteral("dragging upward increases the gain"));
    check(std::abs(afterRelease - afterMove) < 0.001,
          QStringLiteral("release does not snap the value back"));

    // Put it back: this runs against the user's live session, and a test that
    // silently leaves band 0 at +8 dB would be a nasty surprise next launch.
    ctl->bands()->setGain(0, before);
    ctl->flushNow();
    check(std::abs(ctl->bands()->gain(0) - before) < 0.001,
          QStringLiteral("original value restored"));

    // ---- A/B comparison. Runs against the live session too, so it puts every
    // value back and checks that switching is lossless: a snapshot that drops
    // a setting would make A and B differ in ways nobody changed.
    {
        out() << "\n[A/B]" << Qt::endl;
        const double preamp0 = ctl->preamp();
        const double gain0 = ctl->bands()->gain(0);
        const bool eq0 = ctl->eqEnabled();
        const bool conv0 = ctl->convolutionEnabled();
        const QString file0 = ctl->convolutionFile();
        const int bands0 = ctl->bands()->rowCount();
        const QVariantList chain0 = ctl->chain();

        ctl->startAb();
        check(ctl->abActive() == 0, QStringLiteral("A/B starts on A"));

        ctl->selectAb(1);
        check(ctl->abActive() == 1, QStringLiteral("switches to B"));
        check(std::abs(ctl->preamp() - preamp0) < 1e-6 && std::abs(ctl->bands()->gain(0) - gain0) < 1e-6,
              QStringLiteral("B starts as a copy of A"));

        ctl->setPreamp(preamp0 - 3.0);
        ctl->bands()->setGain(0, gain0 + 2.0);
        ctl->selectAb(0);
        check(std::abs(ctl->preamp() - preamp0) < 1e-6 && std::abs(ctl->bands()->gain(0) - gain0) < 1e-6,
              QStringLiteral("back on A: B's edits did not leak"));
        check(ctl->eqEnabled() == eq0 && ctl->convolutionEnabled() == conv0
                  && ctl->convolutionFile() == file0 && ctl->bands()->rowCount() == bands0,
              QStringLiteral("A round-trips switches, convolution and band count"));
        check(ctl->chain() == chain0, QStringLiteral("A round-trips the chain order and switches"));

        ctl->selectAb(1);
        check(std::abs(ctl->preamp() - (preamp0 - 3.0)) < 1e-6
                  && std::abs(ctl->bands()->gain(0) - (gain0 + 2.0)) < 1e-6,
              QStringLiteral("B kept its own edits"));

        ctl->copyAb();
        ctl->selectAb(0);
        ctl->selectAb(1);
        check(std::abs(ctl->preamp() - (preamp0 - 3.0)) < 1e-6,
              QStringLiteral("copy B -> A leaves B as it was"));

        ctl->stopAb();
        check(ctl->abActive() == -1, QStringLiteral("A/B stops"));
        ctl->setPreamp(preamp0);
        ctl->bands()->setGain(0, gain0);
        ctl->flushNow();
        check(std::abs(ctl->preamp() - preamp0) < 1e-6, QStringLiteral("session values restored"));
    }

    // ---- Curve source. Runs on the live session too, so everything it touches
    // is snapshotted and put back: this exercises the real import path
    // (file -> bands -> remembered source) rather than the parsers alone.
    {
        const QVector<PresetBand> savedBands = ctl->bands()->bands();
        const double savedPreamp = ctl->preamp();

        QTemporaryDir dir;
        const QString curve = QDir(dir.path()).filePath(QStringLiteral("curve.txt"));
        {
            QFile f(curve);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write("Preamp: -3.0 dB\n"
                    "Filter 1: ON PK Fc 2957 Hz Gain -5.2 dB Q 4.24\n"
                    "Filter 2: ON PK Fc 105 Hz Gain 3.0 dB Q 0.70\n");
        }

        check(ctl->loadEqFile(QUrl::fromLocalFile(curve)),
              QStringLiteral("a dropped file is applied"));
        check(ctl->bands()->rowCount() == 2,
              QStringLiteral("its bands replaced the session's (%1)").arg(ctl->bands()->rowCount()));
        check(ctl->eqSourcePath() == curve,
              QStringLiteral("the file it came from is remembered"));
        check(ctl->eqSourceExists(), QStringLiteral("and it is reported as present"));

        // Edited in place, then reloaded: that is the point of remembering a path.
        {
            QFile f(curve);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write("Preamp: -2.0 dB\n"
                    "Filter 1: ON PK Fc 1000 Hz Gain 1.0 dB Q 1.00\n"
                    "Filter 2: ON PK Fc 2000 Hz Gain -1.0 dB Q 1.00\n"
                    "Filter 3: ON PK Fc 4000 Hz Gain 2.0 dB Q 1.00\n");
        }
        check(ctl->reloadEqSource() && ctl->bands()->rowCount() == 3,
              QStringLiteral("reloading picks up the edited file"));

        // Typed in, not opened: no file, so nothing to remember.
        check(ctl->importGraphicEq(QStringLiteral("GraphicEQ: 20 0.0; 100 3.0; 1000 -1.0; 10000 1.0")),
              QStringLiteral("a pasted curve is applied"));
        check(ctl->eqSourcePath().isEmpty(),
              QStringLiteral("a pasted curve records no source"));

        ctl->clearGraphicEq();
        ctl->bands()->setBands(savedBands);
        ctl->setPreamp(savedPreamp);
        ctl->flushNow();

        // The path is persisted in QSettings, and the file it names is about to
        // disappear with the temporary directory -- a dead source left behind
        // would show up on the settings page of the next real start.
        QSettings settings(QStringLiteral("DreamDSP"), QStringLiteral("DreamDSP"));
        settings.remove(QStringLiteral("eqSourcePath"));

        check(ctl->bands()->rowCount() == savedBands.size(),
              QStringLiteral("the session's bands are back"));
    }

    out() << "\n" << (g_failures == 0 ? "PASS" : "FAIL")
          << " -- " << g_failures << " failure(s)" << Qt::endl;
    out().flush();
    return g_failures;
}

void testEqImport()
{
    out() << "\n[imported curves]" << Qt::endl;

    QTemporaryDir tmp;

    // One band of each shape a FiiO file can hold. Written and read back: the
    // encoding is the part that cannot be eyeballed, and a curve that lands a
    // shelf where the peak was sounds wrong in a way nobody can debug.
    Preset original;
    original.name = QStringLiteral("JT7 → Harman 2018");
    original.description = QStringLiteral("round-trip");
    original.preamp = -3.0;
    original.bands.push_back({ 47.0, 5.4, FiiEqXml::kStandardShelfQ, FilterType::LSC, true });
    original.bands.push_back({ 2967.0, -4.0, 2.17, FilterType::PK, true });
    original.bands.push_back({ 4582.0, -1.4, FiiEqXml::kStandardShelfQ, FilterType::HSC, true });
    original.bands.push_back({ 1113.0, 0.2, 0.5, FilterType::LSCQ, true });

    const QString xmlPath = QDir(tmp.path()).filePath(QStringLiteral("curve.xml"));
    QString err;
    check(FiiEqXml::write(xmlPath, original, QStringLiteral("FIIO K17 R2R"), &err),
          QStringLiteral("wrote a FiiO XML (%1)").arg(err.isEmpty() ? QStringLiteral("ok") : err));

    Preset back;
    QString model;
    check(FiiEqXml::read(xmlPath, &back, &model, &err),
          QStringLiteral("read it back (%1)").arg(err.isEmpty() ? QStringLiteral("ok") : err));
    check(model == QStringLiteral("FIIO K17 R2R"), QStringLiteral("model survives: %1").arg(model));
    check(back.name == original.name, QStringLiteral("styleName survives: %1").arg(back.name));
    check(std::abs(back.preamp - original.preamp) < 1e-6, QStringLiteral("masterGain survives"));
    check(back.bands.size() == original.bands.size(),
          QStringLiteral("%1 of %2 bands")
              .arg(back.bands.size()).arg(original.bands.size()));

    bool same = back.bands.size() == original.bands.size();
    for (int i = 0; same && i < back.bands.size(); ++i) {
        const PresetBand &a = original.bands[i];
        const PresetBand &b = back.bands[i];
        same = std::abs(a.frequency - b.frequency) < 0.01
               && std::abs(a.gainDb - b.gainDb) < 0.01
               && std::abs(a.q - b.q) < 0.001
               && a.type == b.type;
    }
    check(same, QStringLiteral("every band survives the round-trip"));

    // The k19 client's version 1.0 encoding stores register values, not Hz/dB/Q.
    // Reading it as plain numbers would ask for +120 dB, so it has to be
    // refused with an explanation instead.
    const QByteArray encoded =
        "<?xml version=\"1.0\"?>"
        "<FiiO_DSP model=\"K19\" version=\"1.0\"><module name=\"EQ\"><eqGroup>"
        "<eqList><eq index=\"0\">"
        "<param name=\"type\">1</param><param name=\"freq\">84</param>"
        "<param name=\"gain\">167</param><param name=\"q\">16</param>"
        "</eq></eqList></eqGroup></module></FiiO_DSP>";
    Preset v1;
    err.clear();
    const bool encodedRefused = !FiiEqXml::readData(encoded, &v1, nullptr, &err);
    check(encodedRefused && !err.isEmpty(),
          QStringLiteral("refuses the version 1.0 encoding: %1").arg(err));

    // Parametric text, i.e. what AutoEQ and every review table export.
    const QString text = QStringLiteral("Preamp: -3.0 dB\n"
                                        "Filter 1: ON LSC Fc 105 Hz Gain -1.9 dB Q 0.70\n"
                                        "Filter 2: ON PK Fc 2957 Hz Gain -5.2 dB Q 4.24\n");
    check(ParametricEqText::looksLikeParametricEq(text), QStringLiteral("recognised as parametric text"));

    Preset fromText;
    err.clear();
    const bool textRead = ParametricEqText::read(text, &fromText, &err);
    check(textRead && fromText.bands.size() == 2,
          QStringLiteral("parsed %1 filter(s)").arg(fromText.bands.size()));
    if (fromText.bands.size() == 2) {
        check(std::abs(fromText.preamp + 3.0) < 1e-6, QStringLiteral("preamp read"));
        check(fromText.bands[0].type == FilterType::LSC && fromText.bands[1].type == FilterType::PK,
              QStringLiteral("filter types read"));
        check(std::abs(fromText.bands[1].frequency - 2957.0) < 1e-6
                  && std::abs(fromText.bands[1].gainDb + 5.2) < 1e-6
                  && std::abs(fromText.bands[1].q - 4.24) < 1e-6,
              QStringLiteral("frequency/gain/Q read"));
    }

    // A GraphicEQ curve must not be mistaken for one: it has neither keyword.
    const QString graphic = QStringLiteral("GraphicEQ: 20 -1.5; 25 -1.4; 31 -1.2");
    check(!ParametricEqText::looksLikeParametricEq(graphic),
          QStringLiteral("a GraphicEQ curve is not parametric text"));

    // Whatever FiiO files are actually on this machine, parsed for real -- the
    // synthetic document above proves the format, not the corpus.
    QStringList dirs;
    const ApoInstall apo = locateApo();
    if (!apo.configPath.isEmpty())
        dirs << apo.configPath;
    dirs << PresetStore::userDirectory();

    int found = 0;
    for (const QString &dirPath : std::as_const(dirs)) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;
        const QStringList files = dir.entryList({ QStringLiteral("*.xml") }, QDir::Files, QDir::Name);
        for (const QString &file : files) {
            QFile f(dir.filePath(file));
            if (!f.open(QIODevice::ReadOnly) || !FiiEqXml::looksLikeFiiXml(f.readAll()))
                continue;
            ++found;
            Preset p;
            QString e;
            if (FiiEqXml::read(dir.filePath(file), &p, nullptr, &e)) {
                out() << QStringLiteral("    %1  %2 bands  preamp %3 dB")
                             .arg(file, -40).arg(p.bands.size(), 2).arg(p.preamp, 5, 'f', 1)
                      << Qt::endl;
            } else {
                out() << QStringLiteral("    %1  --  %2").arg(file, -40).arg(e) << Qt::endl;
            }
        }
    }
    out() << QStringLiteral("  --   %1 FiiO XML file(s) found in the config and preset folders")
                 .arg(found)
          << Qt::endl;
}

// --------------------------------------------------- incremental updates

void testFileUpdate()
{
    out() << "\n[incremental update]" << Qt::endl;

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        check(false, QStringLiteral("temporary directory"));
        return;
    }

    const QDir root(tmp.path());
    const QString payloadDir = root.filePath(QStringLiteral("payload"));
    const QString installDir = root.filePath(QStringLiteral("install"));
    const QString stageDir = root.filePath(QStringLiteral("stage"));
    QDir().mkpath(installDir);

    const auto writeFile = [](const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        return f.write(bytes) == bytes.size();
    };
    const auto readFile = [](const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };

    // The release's payload: the files that changed, and nothing else. A delta
    // that carried the 40 MB of Qt runtime would not be worth having.
    writeFile(QDir(payloadDir).filePath(QStringLiteral("DreamDSP.exe")),
              QByteArray("new exe bytes"));
    writeFile(QDir(payloadDir).filePath(QStringLiteral("qml/extra.qml")),
              QByteArray("import QtQuick\n"));

    // What is installed now.
    writeFile(QDir(installDir).filePath(QStringLiteral("DreamDSP.exe")),
              QByteArray("old exe bytes"));
    writeFile(QDir(installDir).filePath(QStringLiteral("stale.txt")), QByteArray("gone"));
    writeFile(QDir(installDir).filePath(QStringLiteral("keep.txt")), QByteArray("untouched"));

    const QString container = root.filePath(QStringLiteral("delta.bin"));
    QString err;
    check(FileUpdatePackage::writeFromDirectory(
              container, QStringLiteral("0.6.0"), QStringLiteral("0.5.0"),
              { QStringLiteral("DreamDSP.exe"), QStringLiteral("qml/extra.qml") },
              { QStringLiteral("stale.txt") }, payloadDir, &err),
          QStringLiteral("wrote a delta container (%1)")
              .arg(err.isEmpty() ? QStringLiteral("ok") : err));

    FileUpdate pkg;
    check(FileUpdatePackage::read(container, &pkg, &err),
          QStringLiteral("read it back (%1)").arg(err.isEmpty() ? QStringLiteral("ok") : err));
    check(pkg.version == QStringLiteral("0.6.0") && pkg.base == QStringLiteral("0.5.0"),
          QStringLiteral("version %1 on top of %2").arg(pkg.version, pkg.base));
    check(pkg.files.size() == 2 && pkg.remove.size() == 1,
          QStringLiteral("%1 file(s), %2 to remove").arg(pkg.files.size()).arg(pkg.remove.size()));
    check(pkg.payloadSize() == QFileInfo(container).size() - pkg.payloadOffset,
          QStringLiteral("payload size matches the container"));

    check(FileUpdatePackage::stageInto(container, pkg, stageDir, &err),
          QStringLiteral("staged (%1)").arg(err.isEmpty() ? QStringLiteral("ok") : err));
    check(QFileInfo::exists(QDir(stageDir).filePath(QStringLiteral("plan.json"))),
          QStringLiteral("plan.json written"));

    const FileUpdatePackage::ApplyResult applied =
        FileUpdatePackage::applyFrom(stageDir, installDir);
    check(applied.applied && applied.complete,
          QStringLiteral("applied %1 file(s), nothing left over").arg(applied.replaced.size()));
    check(readFile(QDir(installDir).filePath(QStringLiteral("DreamDSP.exe")))
              == QByteArray("new exe bytes"),
          QStringLiteral("the program file was replaced"));
    check(readFile(QDir(installDir).filePath(QStringLiteral("qml/extra.qml")))
              == QByteArray("import QtQuick\n"),
          QStringLiteral("a new file was created, with its directory"));
    check(!QFileInfo::exists(QDir(installDir).filePath(QStringLiteral("stale.txt"))),
          QStringLiteral("a file the release dropped was removed"));
    check(readFile(QDir(installDir).filePath(QStringLiteral("keep.txt")))
              == QByteArray("untouched"),
          QStringLiteral("a file the package does not mention was left alone"));
    check(!QFileInfo::exists(stageDir), QStringLiteral("the stage directory is cleaned up"));

    // A second apply with nothing staged must do nothing rather than fail.
    const FileUpdatePackage::ApplyResult again =
        FileUpdatePackage::applyFrom(stageDir, installDir);
    check(!again.applied && !again.complete && again.error.isEmpty(),
          QStringLiteral("applying an empty stage is a no-op"));

    // Every file is checked against the header's hash, so a corrupted payload
    // never reaches the install directory -- the run above proves it would
    // otherwise have been swapped in.
    {
        QFile f(container);
        f.open(QIODevice::ReadOnly);
        QByteArray bytes = f.readAll();
        f.close();
        bytes[bytes.size() - 1] = char(bytes.at(bytes.size() - 1) ^ 0x5a);
        const QString tampered = root.filePath(QStringLiteral("tampered.bin"));
        writeFile(tampered, bytes);

        FileUpdate bad;
        const bool readOk = FileUpdatePackage::read(tampered, &bad, &err);
        check(readOk, QStringLiteral("a tampered container still parses (its files do not)"));
        const QString tamperStage = root.filePath(QStringLiteral("stage-tampered"));
        err.clear();
        const bool tainted = FileUpdatePackage::stageInto(tampered, bad, tamperStage, &err);
        check(!tainted && !err.isEmpty(), QStringLiteral("staging refused it: %1").arg(err));
        check(!QFileInfo::exists(QDir(tamperStage).filePath(QStringLiteral("plan.json"))),
              QStringLiteral("nothing was staged from it"));
    }

    // A file mapped by a running process cannot be deleted or overwritten, but
    // it can be renamed -- which is the whole trick the update relies on, and
    // the one thing a test with plain files cannot exercise. So load a DLL that
    // the package is about to replace.
    {
        const QString lockedName = QStringLiteral("locked-test.dll");
        const QString locked = QDir(installDir).filePath(lockedName);
        QFile::remove(locked);

        // DreamDspApo.dll has no Qt in it: a second copy of a Qt DLL inside a
        // Qt process upsets its static destructors at exit, which would make
        // this test itself the bug.
        bool copied = false;
        {
            const QString source = QDir(QCoreApplication::applicationDirPath())
                                       .filePath(QStringLiteral("DreamDspApo.dll"));
            copied = QFileInfo::exists(source) && QFile::copy(source, locked);
        }

        HMODULE module = nullptr;
        if (copied)
            module = ::LoadLibraryW(reinterpret_cast<LPCWSTR>(locked.utf16()));

        if (!module) {
            out() << "  --   could not take a DLL lock; the rename path is untested here"
                  << Qt::endl;
        } else {
            const QString payloadDir2 = root.filePath(QStringLiteral("payload-locked"));
            writeFile(QDir(payloadDir2).filePath(lockedName),
                      QByteArray("replacement bytes for a loaded module"));
            const QString container2 = root.filePath(QStringLiteral("locked.bin"));
            const QString stage2 = root.filePath(QStringLiteral("stage-locked"));

            QString err2;
            const bool wrote = FileUpdatePackage::writeFromDirectory(
                container2, QStringLiteral("0.6.1"), QStringLiteral("0.6.0"),
                { lockedName }, {}, payloadDir2, &err2);
            FileUpdate pkg2;
            const bool readOk = wrote && FileUpdatePackage::read(container2, &pkg2, &err2);
            const bool staged = readOk && FileUpdatePackage::stageInto(container2, pkg2, stage2, &err2);
            check(staged, QStringLiteral("a package that replaces a loaded DLL staged (%1)")
                              .arg(err2.isEmpty() ? QStringLiteral("ok") : err2));

            const FileUpdatePackage::ApplyResult lockedResult =
                FileUpdatePackage::applyFrom(stage2, installDir);
            check(lockedResult.complete && lockedResult.replaced.contains(lockedName),
                  QStringLiteral("the loaded DLL was renamed aside and replaced"));

            QFile f(locked);
            const bool opened = f.open(QIODevice::ReadOnly);
            check(opened && f.readAll() == QByteArray("replacement bytes for a loaded module"),
                  QStringLiteral("the new bytes are on disk while the old image still runs"));
            f.close();

            ::FreeLibrary(module);

            // Now that nothing holds the image, the next start's sweep clears
            // the leftovers without being told which they were.
            // An empty package is a start with nothing to do: the sweep is the
            // only thing it should touch.
            const QString emptyContainer = root.filePath(QStringLiteral("empty.bin"));
            const QString emptyStage = root.filePath(QStringLiteral("stage-empty"));
            FileUpdate emptyPkg;
            QString err3;
            FileUpdatePackage::writeFromDirectory(emptyContainer, QStringLiteral("0.6.1"),
                                                  QStringLiteral("0.6.0"), {}, {},
                                                  root.path(), &err3);
            const bool emptyRead = FileUpdatePackage::read(emptyContainer, &emptyPkg, &err3);
            const bool emptyStaged = emptyRead
                && FileUpdatePackage::stageInto(emptyContainer, emptyPkg, emptyStage, &err3);
            check(emptyStaged, QStringLiteral("staged an empty package (%1)").arg(err3));
            FileUpdatePackage::applyFrom(emptyStage, installDir);
            QDirIterator leftovers(installDir, { QStringLiteral("*.ddsp-old") }, QDir::Files,
                                   QDirIterator::Subdirectories);
            const bool anyLeft = leftovers.hasNext();
            check(!anyLeft, QStringLiteral("renamed-aside leftovers are cleaned up"));
        }
    }

    // An application directory that cannot take a file: the package has to
    // survive the half-done state, keep what is outstanding, and say so --
    // rather than report success, or throw the staged copy away.
    {
        const QString payloadDir3 = root.filePath(QStringLiteral("payload-busy"));
        writeFile(QDir(payloadDir3).filePath(QStringLiteral("busy.txt")), QByteArray("new"));
        const QString container3 = root.filePath(QStringLiteral("busy.bin"));
        const QString stage3 = root.filePath(QStringLiteral("stage-busy"));
        QString err3;
        FileUpdatePackage::writeFromDirectory(container3, QStringLiteral("0.6.2"),
                                              QStringLiteral("0.6.1"),
                                              { QStringLiteral("busy.txt") }, {},
                                              payloadDir3, &err3);
        FileUpdate pkg3;
        const bool readOk3 = FileUpdatePackage::read(container3, &pkg3, &err3);
        check(readOk3 && FileUpdatePackage::stageInto(container3, pkg3, stage3, &err3),
              QStringLiteral("staged a package whose target is held open"));

        // Hold the target with no sharing at all: Windows then refuses both the
        // delete and the rename, which is the one case the updater cannot work
        // around.
        writeFile(QDir(installDir).filePath(QStringLiteral("busy.txt")), QByteArray("old"));
        const QString busyPath = QDir(installDir).filePath(QStringLiteral("busy.txt"));
        HANDLE held = ::CreateFileW(reinterpret_cast<LPCWSTR>(busyPath.utf16()),
                                    GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE) {
            out() << "  --   could not hold a file exclusively; the retry path is untested"
                  << Qt::endl;
        } else {
            const FileUpdatePackage::ApplyResult busy =
                FileUpdatePackage::applyFrom(stage3, installDir);
            check(!busy.complete && busy.failed.contains(QStringLiteral("busy.txt"))
                      && !busy.error.isEmpty(),
                  QStringLiteral("a file that cannot be released is reported: %1").arg(busy.error));
            check(QFileInfo::exists(QDir(stage3).filePath(QStringLiteral("plan.json"))),
                  QStringLiteral("the package is kept for the next start"));

            // And the next start, with the file free, finishes the job.
            ::CloseHandle(held);
            const FileUpdatePackage::ApplyResult retry =
                FileUpdatePackage::applyFrom(stage3, installDir);
            check(retry.complete && retry.replaced.contains(QStringLiteral("busy.txt")),
                  QStringLiteral("the next start finishes it"));
            check(!QFileInfo::exists(stage3), QStringLiteral("and clears the stage"));
        }
    }

    // A package that tries to write outside the directory it is applied to is
    // refused at parse time, before anything is staged or renamed.
    {
        const QByteArray header = R"({"version":"9.9.9","base":"0.5.0",)"
                                  R"("files":[{"path":"../evil.txt","size":1,)"
                                  R"("sha256":"0000000000000000000000000000000000000000000000000000000000000000"}],)"
                                  R"("remove":[]})";
        const QString escaped = root.filePath(QStringLiteral("escape.bin"));
        QFile f(escaped);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write("DDUPD1\n");
        const quint32 len = quint32(header.size());
        QByteArray lenBytes;
        lenBytes.append(char((len >> 24) & 0xff));
        lenBytes.append(char((len >> 16) & 0xff));
        lenBytes.append(char((len >> 8) & 0xff));
        lenBytes.append(char(len & 0xff));
        f.write(lenBytes);
        f.write(header);
        f.close();

        FileUpdate escapedPkg;
        err.clear();
        const bool escapedOk = FileUpdatePackage::read(escaped, &escapedPkg, &err);
        check(!escapedOk && !err.isEmpty(),
              QStringLiteral("a path escaping the install directory is refused: %1").arg(err));
    }

    // Not a container at all: the updater must not mistake an ordinary file for
    // one, because everything after the header trusts it.
    {
        const QString junk = root.filePath(QStringLiteral("junk.bin"));
        writeFile(junk, QByteArray("<?xml version=\"1.0\"?><FiiO_DSP/>"));
        FileUpdate junkPkg;
        err.clear();
        const bool junkOk = FileUpdatePackage::read(junk, &junkPkg, &err);
        check(!junkOk && !err.isEmpty(),
              QStringLiteral("a file that is not a container is refused: %1").arg(err));
    }
}

int runSelfTest()
{
    g_failures = 0;
    out() << "DreamDSP self-test" << Qt::endl;

    const ApoInstall apo = locateApo();
    testEnvironment(apo);
    testBiquad();
    testFft();
    testApoConfig();
    testVersionCompare();
    testAutostart();
    testGraphicCurve();
    testFullPreset();
    testPresets(apo);
    testAutoEq(apo);
    testEqImport();
    testFileUpdate();

    out() << "\n" << (g_failures == 0 ? "PASS" : "FAIL")
          << " -- " << g_failures << " failure(s)" << Qt::endl;
    out().flush();
    return g_failures;
}

} // namespace dreamdsp
