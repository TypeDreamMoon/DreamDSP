#include "core/PeacePreset.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace dreamdsp {

namespace {

// A deliberately small INI reader rather than QSettings: .peace files contain
// values like `Slider1-1="` (a lone double quote meaning "inherit"), which
// QSettings' quoting rules mangle.
class Ini
{
public:
    bool load(const QString &path, QString *error)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error) *error = f.errorString();
            return false;
        }
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);

        QString section;
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('#')))
                continue;
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                section = line.mid(1, line.size() - 2).trimmed().toLower();
                continue;
            }
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            m_data[section].insert(line.left(eq).trimmed().toLower(), line.mid(eq + 1).trimmed());
        }
        return true;
    }

    bool hasSection(const QString &s) const { return m_data.contains(s.toLower()); }

    QString value(const QString &section, const QString &key) const
    {
        const auto it = m_data.constFind(section.toLower());
        if (it == m_data.constEnd())
            return {};
        return it->value(key.toLower());
    }

    // Highest N for which "<prefix>N" exists in the section, scanning from 1.
    int countIndexed(const QString &section, const QString &prefix) const
    {
        const auto it = m_data.constFind(section.toLower());
        if (it == m_data.constEnd())
            return 0;
        int n = 0;
        const QString lower = prefix.toLower();
        while (it->contains(lower + QString::number(n + 1)))
            ++n;
        return n;
    }

private:
    QHash<QString, QHash<QString, QString>> m_data;
};

double toDouble(const QString &s, double fallback)
{
    if (s.isEmpty())
        return fallback;
    // Peace writes with a period; be tolerant of comma decimals anyway.
    bool ok = false;
    const double v = QString(s).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    return ok ? v : fallback;
}

} // namespace

bool PeaceFile::read(const QString &path, Preset *out, QString *error)
{
    if (!out) {
        if (error) *error = QStringLiteral("null output");
        return false;
    }

    Ini ini;
    if (!ini.load(path, error))
        return false;

    Preset p;
    p.name = QFileInfo(path).completeBaseName();
    p.description = ini.value(QStringLiteral("General"), QStringLiteral("Description"));
    p.preamp = toDouble(ini.value(QStringLiteral("General"), QStringLiteral("PreAmp0")), 0.0);

    const int count = ini.countIndexed(QStringLiteral("Frequencies"), QStringLiteral("Frequency"));
    if (count <= 0) {
        if (error) *error = QStringLiteral("no [Frequencies] section");
        return false;
    }

    p.bands.reserve(count);
    for (int i = 1; i <= count; ++i) {
        const QString n = QString::number(i);
        PresetBand b;
        b.frequency = toDouble(ini.value(QStringLiteral("Frequencies"), QStringLiteral("Frequency") + n), 1000.0);
        b.gainDb    = toDouble(ini.value(QStringLiteral("Gains"),       QStringLiteral("Gain") + n), 0.0);
        b.q         = toDouble(ini.value(QStringLiteral("Qualities"),   QStringLiteral("Quality") + n), 1.41);

        const QString filter = ini.value(QStringLiteral("Filters"), QStringLiteral("Filter") + n);
        if (!filter.isEmpty())
            b.type = filterTypeFromIndex(filter.toInt());

        b.enabled = ini.value(QStringLiteral("Disabled"), QStringLiteral("Disabled") + n) != QLatin1String("1");

        p.bands.push_back(b);
    }

    *out = p;
    return true;
}

bool PeaceFile::write(const QString &path, const Preset &preset, QString *error)
{
    if (!preset.isValid()) {
        if (error) *error = QStringLiteral("empty preset");
        return false;
    }

    const int n = preset.bands.size();
    const auto num = [](double v) {
        // Trim trailing zeros the way Peace does ("6" not "6.00").
        QString s = QString::number(v, 'f', 2);
        while (s.contains(QLatin1Char('.')) && (s.endsWith(QLatin1Char('0')) || s.endsWith(QLatin1Char('.'))))
            s.chop(1);
        return s;
    };

    QString text;
    text += QStringLiteral("[General]\r\n");
    text += QStringLiteral("Description=%1\r\n").arg(preset.description);
    if (!qFuzzyIsNull(preset.preamp))
        text += QStringLiteral("PreAmp0=%1\r\n").arg(num(preset.preamp));

    // Peace mirrors slot 0 into slots 1..8; emit the same so the file opens
    // cleanly in Peace as well.
    const auto emitBlock = [&](const QString &suffix) {
        text += QStringLiteral("[Frequencies%1]\r\n").arg(suffix);
        for (int i = 0; i < n; ++i)
            text += QStringLiteral("Frequency%1=%2\r\n").arg(i + 1).arg(num(preset.bands[i].frequency));

        text += QStringLiteral("[Qualities%1]\r\n").arg(suffix);
        for (int i = 0; i < n; ++i)
            text += QStringLiteral("Quality%1=%2\r\n").arg(i + 1).arg(num(preset.bands[i].q));

        bool anyGain = false;
        for (const PresetBand &b : preset.bands)
            if (!qFuzzyIsNull(b.gainDb)) { anyGain = true; break; }
        if (anyGain) {
            text += QStringLiteral("[Gains%1]\r\n").arg(suffix);
            for (int i = 0; i < n; ++i)
                text += QStringLiteral("Gain%1=%2\r\n").arg(i + 1).arg(num(preset.bands[i].gainDb));
        }

        bool anyFilter = false;
        for (const PresetBand &b : preset.bands)
            if (b.type != FilterType::PK) { anyFilter = true; break; }
        if (anyFilter) {
            text += QStringLiteral("[Filters%1]\r\n").arg(suffix);
            for (int i = 0; i < n; ++i)
                if (preset.bands[i].type != FilterType::PK)
                    text += QStringLiteral("Filter%1=%2\r\n").arg(i + 1).arg(filterTypeIndex(preset.bands[i].type));
        }
    };

    emitBlock(QString());
    for (int slot = 1; slot <= 8; ++slot)
        emitBlock(QString::number(slot));

    bool anyDisabled = false;
    for (const PresetBand &b : preset.bands)
        if (!b.enabled) { anyDisabled = true; break; }
    if (anyDisabled) {
        text += QStringLiteral("[Disabled]\r\n");
        for (int i = 0; i < n; ++i)
            if (!preset.bands[i].enabled)
                text += QStringLiteral("Disabled%1=1\r\n").arg(i + 1);
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    if (f.write(bytes) != bytes.size() || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace dreamdsp
