#include "app/PresetStore.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QVariantMap>

#include <algorithm>

namespace dreamdsp {

namespace {

// Peace's own session/scratch files live in the APO config directory next to
// the real presets; showing them as user-selectable presets is just noise.
bool isNoiseName(const QString &base)
{
    static const QStringList skip = {
        QStringLiteral("Last Configuration"),
    };
    return skip.contains(base, Qt::CaseInsensitive);
}

QString sanitise(const QString &name)
{
    QString out = name.trimmed();
    for (QChar &c : out) {
        if (QStringLiteral("\\/:*?\"<>|").contains(c))
            c = QLatin1Char('_');
    }
    return out;
}

} // namespace

PresetStore::PresetStore(QObject *parent)
    : QAbstractListModel(parent)
{
}

QString PresetStore::userDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("presets"));
}

int PresetStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant PresetStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case NameRole:        return e.name;
    case DescriptionRole: return e.description;
    case PathRole:        return e.path;
    case ImportedRole:    return e.imported;
    case BandCountRole:   return e.bandCount;
    default:              return {};
    }
}

QHash<int, QByteArray> PresetStore::roleNames() const
{
    return {
        { NameRole,        "name" },
        { DescriptionRole, "description" },
        { PathRole,        "path" },
        { ImportedRole,    "imported" },
        { BandCountRole,   "bandCount" },
    };
}

QVariantList PresetStore::entries() const
{
    QVariantList out;
    out.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        out.append(QVariantMap{
            { QStringLiteral("value"), i },
            { QStringLiteral("name"), e.name },   // raw, for matching
            { QStringLiteral("label"), e.imported
                  ? QStringLiteral("%1  · 来自 Peace").arg(e.name)
                  : e.name },
            { QStringLiteral("imported"), e.imported },
            { QStringLiteral("description"), e.description },
            { QStringLiteral("bandCount"), e.bandCount },
        });
    }
    return out;
}

void PresetStore::setImportDirectory(const QString &dir)
{
    if (m_importDir == dir)
        return;
    m_importDir = dir;
    refresh();
}

void PresetStore::scan(const QString &dir, bool imported)
{
    if (dir.isEmpty())
        return;

    QDir d(dir);
    if (!d.exists())
        return;

    const QStringList files = d.entryList({ QStringLiteral("*.peace") }, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        const QString path = d.filePath(file);
        Preset p;
        if (!PeaceFile::read(path, &p))
            continue;   // unreadable or not an EQ preset -- skip quietly

        Entry e;
        e.name = p.name;
        if (imported && isNoiseName(e.name))
            continue;
        e.description = p.description;
        e.path = path;
        e.imported = imported;
        e.bandCount = p.bands.size();
        m_entries.push_back(e);
    }
}

void PresetStore::refresh()
{
    beginResetModel();
    m_entries.clear();
    scan(userDirectory(), false);
    scan(m_importDir, true);
    endResetModel();
    emit countChanged();
}

bool PresetStore::load(int row, Preset *out, QString *error) const
{
    if (row < 0 || row >= m_entries.size()) {
        if (error) *error = QStringLiteral("preset index out of range");
        return false;
    }
    return PeaceFile::read(m_entries.at(row).path, out, error);
}

bool PresetStore::save(const QString &name, const Preset &preset, QString *error)
{
    const QString clean = sanitise(name);
    if (clean.isEmpty()) {
        if (error) *error = QStringLiteral("预设名不能为空");
        return false;
    }

    QDir dir(userDirectory());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error) *error = QStringLiteral("无法创建预设目录 %1").arg(dir.path());
        return false;
    }

    Preset p = preset;
    p.name = clean;
    if (!PeaceFile::write(dir.filePath(clean + QStringLiteral(".peace")), p, error))
        return false;

    refresh();
    return true;
}

bool PresetStore::remove(int row, QString *error)
{
    if (row < 0 || row >= m_entries.size()) {
        if (error) *error = QStringLiteral("预设索引越界");
        return false;
    }
    const Entry &e = m_entries.at(row);
    if (e.imported) {
        // Never delete out of Equalizer APO's own directory -- those belong to
        // the user's Peace install, not to us.
        if (error) *error = QStringLiteral("导入的预设是只读的,不能删除");
        return false;
    }
    if (!QFile::remove(e.path)) {
        if (error) *error = QStringLiteral("删除失败: %1").arg(e.path);
        return false;
    }
    refresh();
    return true;
}

} // namespace dreamdsp
