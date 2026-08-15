#include "app/SelfTest.h"

#include "app/AppController.h"
#include "app/PresetStore.h"
#include "platform/Autostart.h"
#include "core/ApoConfig.h"
#include "core/AutoEqDatabase.h"
#include "core/Biquad.h"
#include "core/Fft.h"
#include "core/DreamPreset.h"
#include "core/PeacePreset.h"
#include "platform/ApoLocator.h"
#include "platform/AudioDevices.h"
#include "platform/UpdateChecker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cmath>
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
    check(apo.found, QStringLiteral("Equalizer APO found: %1 (v%2)")
                         .arg(apo.configPath, apo.version));
    check(apo.configWritable, QStringLiteral("config directory is writable without elevation"));

    QString err;
    const auto devices = enumerateRenderDevices(&err);
    check(!devices.isEmpty(), QStringLiteral("enumerated %1 render endpoint(s)").arg(devices.size()));
    for (const AudioDevice &d : devices) {
        out() << QStringLiteral("    %1%2  %3")
                     .arg(d.isDefault ? QStringLiteral("* ") : QStringLiteral("  "))
                     .arg(d.name, -44)
                     .arg(d.active ? QStringLiteral("active") : QStringLiteral("inactive"))
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
    if (!db.load(apo.configPath, &err)) {
        check(false, QStringLiteral("load databases: %1").arg(err));
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

    out() << "\n" << (g_failures == 0 ? "PASS" : "FAIL")
          << " -- " << g_failures << " failure(s)" << Qt::endl;
    out().flush();
    return g_failures;
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
    testFullPreset();
    testPresets(apo);
    testAutoEq(apo);

    out() << "\n" << (g_failures == 0 ? "PASS" : "FAIL")
          << " -- " << g_failures << " failure(s)" << Qt::endl;
    out().flush();
    return g_failures;
}

} // namespace dreamdsp
