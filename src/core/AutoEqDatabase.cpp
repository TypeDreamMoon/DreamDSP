#include "core/AutoEqDatabase.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

#include <algorithm>

namespace dreamdsp {

namespace {

// Fixed-band variants always use these ten frequencies at Q 1.41
// (Peace.au3:593-594).
const double kFixedFrequencies[] = { 31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
constexpr double kFixedQ = 1.41;

struct SourceFile {
    const char *file;
    const char *name;
};

constexpr SourceFile kSources[] = {
    { "AutoEQCompressedHarman5.txt",  "Harman" },
    { "AutoEQCompressedIEF5.txt",     "IEF" },
    { "AutoEQCompressedIEFBass5.txt", "IEF Bass" },
    { "OPRAEQCompressed5.txt",        "OPRA" },
};

// Reads a number starting at `pos`, stopping at the next tag letter. Returns
// the value and advances `pos` past it.
double readNumber(const QString &s, int &pos)
{
    const int start = pos;
    if (pos < s.size() && (s.at(pos) == QLatin1Char('-') || s.at(pos) == QLatin1Char('+')))
        ++pos;
    while (pos < s.size()
           && (s.at(pos).isDigit() || s.at(pos) == QLatin1Char('.'))) {
        ++pos;
    }
    return s.mid(start, pos - start).toDouble();
}

} // namespace

QString AutoEqEntry::displayName() const
{
    QString out = device;
    if (!measurer.isEmpty())
        out += QStringLiteral("  ·  %1").arg(measurer);
    if (!target.isEmpty())
        out += QStringLiteral("  ·  %1").arg(target);
    return out;
}

QStringList AutoEqDatabase::sourceNames()
{
    QStringList out;
    for (const SourceFile &s : kSources)
        out << QString::fromLatin1(s.name);
    return out;
}

bool AutoEqDatabase::load(const QString &configDir, QString *error)
{
    m_entries.clear();
    m_loaded = false;

    if (configDir.isEmpty()) {
        if (error) *error = QStringLiteral("未知的 APO 配置目录");
        return false;
    }

    QDir dir(configDir);
    int found = 0;
    for (int i = 0; i < static_cast<int>(std::size(kSources)); ++i) {
        const QString path = dir.filePath(QString::fromLatin1(kSources[i].file));
        if (!QFile::exists(path))
            continue;
        if (loadFile(path, i, error))
            ++found;
    }

    if (found == 0) {
        if (error && error->isEmpty())
            *error = QStringLiteral("在 %1 里没有找到 AutoEQ 数据库").arg(configDir);
        return false;
    }

    m_loaded = true;
    return true;
}

bool AutoEqDatabase::loadFile(const QString &path, int sourceIndex, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    QString measurer, target;
    AutoEqEntry current;
    bool haveDevice = false;

    const auto flush = [&] {
        if (haveDevice && !current.parametric.isEmpty())
            m_entries.push_back(current);
        haveDevice = false;
    };

    // Skip the "<version> <count>" header.
    if (!in.atEnd())
        in.readLine();

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty())
            continue;

        switch (line.at(0).toLatin1()) {
        case 'M':
            flush();
            measurer = line.mid(1);
            break;
        case 'W':
            flush();
            target = line.mid(1);
            break;
        case 'D':
            flush();
            current = AutoEqEntry{};
            current.device = line.mid(1);
            current.measurer = measurer;
            current.target = target;
            current.source = sourceIndex;
            haveDevice = true;
            break;
        case 'A':
            if (!haveDevice)
                break;
            // A fixed-band line is the one containing 'F' filters; the
            // parametric line uses P/L/H/M/I instead.
            if (line.contains(QLatin1Char('F')))
                current.fixedBand = line;
            else if (current.parametric.isEmpty())
                current.parametric = line;
            break;
        default:
            // 'R' response curves and 'O' OPRA paths are not needed here.
            break;
        }
    }
    flush();
    return true;
}

QVector<int> AutoEqDatabase::search(const QString &needle, int limit) const
{
    QVector<int> hits;
    const QString n = needle.trimmed();

    for (int i = 0; i < m_entries.size(); ++i) {
        const AutoEqEntry &e = m_entries.at(i);
        if (n.isEmpty()
            || e.device.contains(n, Qt::CaseInsensitive)
            || e.measurer.contains(n, Qt::CaseInsensitive)
            || e.target.contains(n, Qt::CaseInsensitive)) {
            hits.append(i);
            if (hits.size() >= limit)
                break;
        }
    }
    return hits;
}

bool AutoEqDatabase::toPreset(const AutoEqEntry &entry, bool fixedBand, Preset *out)
{
    if (!out)
        return false;

    const QString line = fixedBand ? entry.fixedBand : entry.parametric;
    if (line.isEmpty() || line.at(0) != QLatin1Char('A'))
        return false;

    Preset p;
    p.name = entry.device;
    p.description = QStringLiteral("AutoEQ · %1 · %2").arg(entry.measurer, entry.target);

    int pos = 1;
    p.preamp = readNumber(line, pos);

    int fixedIndex = 0;
    while (pos < line.size()) {
        const char tag = line.at(pos).toLatin1();
        ++pos;

        PresetBand b;
        switch (tag) {
        case 'F': {
            // Fixed band: only a gain, frequency comes from the table.
            if (fixedIndex >= static_cast<int>(std::size(kFixedFrequencies)))
                return !p.bands.isEmpty() ? (*out = p, true) : false;
            b.type = FilterType::PK;
            b.frequency = kFixedFrequencies[fixedIndex++];
            b.gainDb = readNumber(line, pos);
            b.q = kFixedQ;
            p.bands.push_back(b);
            continue;
        }
        case 'P': b.type = FilterType::PK;  break;
        case 'L': b.type = FilterType::LS;  break;
        case 'H': b.type = FilterType::HS;  break;
        case 'M': b.type = FilterType::LSC; break;
        case 'I': b.type = FilterType::HSC; break;
        default:
            // Unknown tag -- skip it rather than mis-parsing the rest.
            continue;
        }

        b.frequency = readNumber(line, pos);

        if (pos < line.size() && line.at(pos) == QLatin1Char('G')) {
            ++pos;
            b.gainDb = readNumber(line, pos);
        }
        // Q is optional on shelves.
        if (pos < line.size() && line.at(pos) == QLatin1Char('Q')) {
            ++pos;
            b.q = readNumber(line, pos);
        } else {
            b.q = 0.707;
        }

        // A handful of upstream records are malformed -- e.g. Fostex T-X0 ends
        // "...Q615000", where a filter tag went missing and the next
        // frequency ran into the Q. Peace reads those the same way; clamp so a
        // corrupt record yields a harmless filter instead of an infinitely
        // narrow spike.
        b.frequency = std::clamp(b.frequency, 1.0, 24000.0);
        b.q = std::clamp(b.q, 0.05, 100.0);
        b.gainDb = std::clamp(b.gainDb, -40.0, 40.0);

        p.bands.push_back(b);
    }

    if (p.bands.isEmpty())
        return false;

    *out = p;
    return true;
}

} // namespace dreamdsp
