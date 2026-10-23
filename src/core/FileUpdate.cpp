#include "core/FileUpdate.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstring>

namespace dreamdsp {

namespace {

// "DDUPD1" plus a newline: the newline is there so that a truncated or
// text-mangled file cannot look like a valid magic.
constexpr char kMagic[] = "DDUPD1\n";
constexpr int kMagicLen = 7;
constexpr int kLengthPrefix = 4;
// A header is a few hundred bytes of JSON. Anything in the megabytes is either
// a format this reader does not know or a file that is not one of these.
constexpr quint32 kMaxHeader = 4u * 1024u * 1024u;

constexpr qint64 kChunk = 1 << 20;

// Files renamed out of the way because they were in use. The suffix names the
// version that put them there, so a failed cleanup is identifiable.
QString asideSuffix() { return QStringLiteral(".ddsp-old"); }

QByteArray hashFile(const QString &path, bool *ok = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(kChunk);
        if (chunk.isEmpty() && f.error() != QFile::NoError) {
            if (ok) *ok = false;
            return {};
        }
        hash.addData(chunk);
    }
    if (ok) *ok = true;
    return hash.result().toHex();
}

// A package may only name files inside the directory it is applied to.
bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 512)
        return false;
    if (path.contains(QLatin1Char('\\')))   // '/'-separated on the wire
        return false;
    if (path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1String("../")))
        return false;
    if (path.contains(QLatin1String("/../")) || path.endsWith(QLatin1String("/..")))
        return false;
    if (path.contains(QLatin1Char(':')))    // drive letters, streams
        return false;
    // Control characters would make the plan unreadable in a log.
    for (const QChar c : path) {
        if (c.unicode() < 0x20)
            return false;
    }
    return QDir::cleanPath(path) == path;
}

quint32 readBigEndian(const QByteArray &bytes)
{
    return (quint32(quint8(bytes.at(0))) << 24) | (quint32(quint8(bytes.at(1))) << 16)
           | (quint32(quint8(bytes.at(2))) << 8) | quint32(quint8(bytes.at(3)));
}

QByteArray writeBigEndian(quint32 value)
{
    QByteArray out(4, Qt::Uninitialized);
    out[0] = char((value >> 24) & 0xff);
    out[1] = char((value >> 16) & 0xff);
    out[2] = char((value >> 8) & 0xff);
    out[3] = char(value & 0xff);
    return out;
}

QJsonObject entryToJson(const FileUpdateEntry &e)
{
    return QJsonObject{ { QStringLiteral("path"), e.path },
                        { QStringLiteral("size"), double(e.size) },
                        { QStringLiteral("sha256"), e.sha256 } };
}

bool planFromJson(const QJsonObject &root, FileUpdate *out, QString *error)
{
    FileUpdate pkg;
    pkg.version = root.value(QStringLiteral("version")).toString();
    pkg.base = root.value(QStringLiteral("base")).toString();

    for (const QJsonValue &v : root.value(QStringLiteral("files")).toArray()) {
        const QJsonObject o = v.toObject();
        FileUpdateEntry e;
        e.path = o.value(QStringLiteral("path")).toString();
        e.size = qint64(o.value(QStringLiteral("size")).toDouble());
        e.sha256 = o.value(QStringLiteral("sha256")).toString().toLower();

        if (!isSafeRelativePath(e.path)) {
            if (error) *error = QStringLiteral("包里的路径不合法:%1").arg(e.path);
            return false;
        }
        if (e.size < 0 || e.size > (qint64(1) << 32)) {
            if (error) *error = QStringLiteral("%1 的大小不合理").arg(e.path);
            return false;
        }
        if (e.sha256.size() != 64) {
            if (error) *error = QStringLiteral("%1 没有 SHA-256").arg(e.path);
            return false;
        }
        pkg.files.push_back(e);
    }

    for (const QJsonValue &v : root.value(QStringLiteral("remove")).toArray()) {
        const QString path = v.toString();
        if (!isSafeRelativePath(path)) {
            if (error) *error = QStringLiteral("包里的待删路径不合法:%1").arg(path);
            return false;
        }
        pkg.remove << path;
    }

    if (pkg.version.isEmpty()) {
        if (error) *error = QStringLiteral("包里没有版本号");
        return false;
    }

    *out = pkg;
    return true;
}

QString planPath(const QString &stageDir)
{
    return QDir(stageDir).filePath(QStringLiteral("plan.json"));
}

bool readPlan(const QString &stageDir, FileUpdate *out, QString *error)
{
    QFile f(planPath(stageDir));
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("打不开 %1:%2").arg(f.fileName(), f.errorString());
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("plan.json 无法解析");
        return false;
    }
    return planFromJson(doc.object(), out, error);
}

bool writePlan(const QString &stageDir, const FileUpdate &pkg, QString *error)
{
    QJsonArray files;
    for (const FileUpdateEntry &e : pkg.files)
        files.append(entryToJson(e));

    QJsonObject root{
        { QStringLiteral("version"), pkg.version },
        { QStringLiteral("base"), pkg.base },
        { QStringLiteral("files"), files },
        { QStringLiteral("remove"), QJsonArray::fromStringList(pkg.remove) },
    };

    QSaveFile f(planPath(stageDir));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

void removeTree(const QString &dir)
{
    QDir(dir).removeRecursively();
}

// Swaps one file in. A file that is mapped by a running process cannot be
// deleted or overwritten, but it can be renamed -- so the old one is moved
// aside, the new one written in its place, and the leftovers swept up on a
// later run when nothing holds them.
bool replaceOne(const QString &stagedPath, const QString &target, const QString &sha256)
{
    QDir().mkpath(QFileInfo(target).absolutePath());

    // A retry after a partial application: the file is already right.
    if (QFileInfo::exists(target)) {
        bool ok = false;
        if (hashFile(target, &ok) == sha256 && ok)
            return true;
    }

    const QString aside = target + asideSuffix();
    if (QFileInfo::exists(target)) {
        QFile::remove(aside);
        if (!QFile::remove(target)) {
            if (QFile::exists(target)) {
                if (!QFile::rename(target, aside))
                    return false;
            }
        }
    }

    if (!QFile::copy(stagedPath, target))
        return false;

    bool ok = false;
    const QByteArray written = hashFile(target, &ok);
    if (!ok || written != sha256) {
        // The write landed on top of something that is not what we staged.
        // Report it as a failure rather than leave a wrong binary in place.
        QFile::remove(target);
        return false;
    }
    return true;
}

bool removeOne(const QString &target)
{
    if (!QFileInfo::exists(target))
        return true;

    if (QFile::remove(target))
        return true;

    // Locked by the running process; move it out of the way instead. The file
    // it belonged to is going away entirely, so this is the whole job.
    const QString aside = target + asideSuffix();
    QFile::remove(aside);
    return QFile::rename(target, aside);
}

// Leftovers from the previous run -- now that this process has just started,
// the files it held last time are normally free again.
void sweepAsides(const QString &root, int depth = 6)
{
    QDirIterator it(root, { QStringLiteral("*.ddsp-old") },
                    QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        // Only the top few levels: the payload is shallow, and walking a deep
        // user directory to delete files is not a thing an updater should do.
        if (QDir(root).relativeFilePath(path).count(QLatin1Char('/')) > depth)
            continue;
        QFile::remove(path);
    }
}

bool s_applied = false;
QString s_appliedVersion;

} // namespace

qint64 FileUpdate::payloadSize() const
{
    qint64 total = 0;
    for (const FileUpdateEntry &e : files)
        total += e.size;
    return total;
}

QString FileUpdatePackage::stageDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("pending"));
}

bool FileUpdatePackage::read(const QString &containerPath, FileUpdate *out, QString *error)
{
    if (!out) {
        if (error) *error = QStringLiteral("没有可写入的目标");
        return false;
    }

    QFile f(containerPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("打不开 %1:%2").arg(containerPath, f.errorString());
        return false;
    }

    const QByteArray magic = f.read(kMagicLen);
    if (magic.size() != kMagicLen || std::memcmp(magic.constData(), kMagic, kMagicLen) != 0) {
        if (error) *error = QStringLiteral("不是增量更新包(文件头不对)");
        return false;
    }

    const QByteArray lengthBytes = f.read(kLengthPrefix);
    if (lengthBytes.size() != kLengthPrefix) {
        if (error) *error = QStringLiteral("增量更新包被截断");
        return false;
    }
    const quint32 headerLength = readBigEndian(lengthBytes);
    if (headerLength == 0 || headerLength > kMaxHeader) {
        if (error) *error = QStringLiteral("增量更新包的头长度不合理");
        return false;
    }

    const QByteArray header = f.read(headerLength);
    if (quint32(header.size()) != headerLength) {
        if (error) *error = QStringLiteral("增量更新包被截断");
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(header);
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("增量更新包的头无法解析");
        return false;
    }

    FileUpdate pkg;
    if (!planFromJson(doc.object(), &pkg, error))
        return false;

    pkg.payloadOffset = qint64(kMagicLen) + kLengthPrefix + headerLength;
    if (f.size() - pkg.payloadOffset < pkg.payloadSize()) {
        if (error) {
            *error = QStringLiteral("增量更新包不完整:应有 %1 字节负载,文件里只有 %2")
                         .arg(pkg.payloadSize())
                         .arg(f.size() - pkg.payloadOffset);
        }
        return false;
    }

    *out = pkg;
    return true;
}

bool FileUpdatePackage::stage(const QString &containerPath, const FileUpdate &pkg, QString *error)
{
    return stageInto(containerPath, pkg, stageDirectory(), error);
}

bool FileUpdatePackage::stageInto(const QString &containerPath, const FileUpdate &pkg,
                                 const QString &stageDir, QString *error)
{
    QFile f(containerPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("打不开 %1:%2").arg(containerPath, f.errorString());
        return false;
    }

    removeTree(stageDir);
    if (!QDir().mkpath(stageDir)) {
        if (error) *error = QStringLiteral("无法创建 %1").arg(stageDir);
        return false;
    }

    qint64 offset = pkg.payloadOffset;
    for (const FileUpdateEntry &e : pkg.files) {
        if (!f.seek(offset)) {
            if (error) *error = QStringLiteral("读不了 %1 的位置").arg(e.path);
            return false;
        }

        const QString target = QDir(stageDir).filePath(e.path);
        QDir().mkpath(QFileInfo(target).absolutePath());

        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) *error = QStringLiteral("写不了 %1:%2").arg(target, out.errorString());
            return false;
        }

        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 remaining = e.size;
        while (remaining > 0) {
            const QByteArray chunk = f.read(qMin(remaining, kChunk));
            if (chunk.isEmpty()) {
                if (error) *error = QStringLiteral("%1 的负载被截断").arg(e.path);
                return false;
            }
            hash.addData(chunk);
            if (out.write(chunk) != chunk.size()) {
                if (error) *error = QStringLiteral("写不了 %1:%2").arg(target, out.errorString());
                return false;
            }
            remaining -= chunk.size();
        }

        if (hash.result().toHex() != e.sha256) {
            // Nothing is left behind: the file never reached the stage tree.
            if (error) *error = QStringLiteral("%1 的 SHA-256 与包头不符").arg(e.path);
            return false;
        }
        if (!out.commit()) {
            if (error) *error = QStringLiteral("写不了 %1:%2").arg(target, out.errorString());
            return false;
        }

        offset += e.size;
    }

    if (!writePlan(stageDir, pkg, error)) {
        removeTree(stageDir);
        return false;
    }
    return true;
}

bool FileUpdatePackage::isStaged()
{
    return QFileInfo::exists(planPath(stageDirectory()));
}

bool FileUpdatePackage::lastApplyFailed()
{
    return QFileInfo::exists(QDir(stageDirectory()).filePath(QStringLiteral("last-error.txt")));
}

QString FileUpdatePackage::stagedVersion()
{
    FileUpdate pkg;
    if (!readPlan(stageDirectory(), &pkg, nullptr))
        return {};
    return pkg.version;
}

bool FileUpdatePackage::appliedThisRun()
{
    return s_applied;
}

QString FileUpdatePackage::appliedVersionThisRun()
{
    return s_appliedVersion;
}

FileUpdatePackage::ApplyResult FileUpdatePackage::applyPending(const QString &installDir)
{
    // Leftovers are swept on every start, not only when there is a package:
    // the files a previous run had to rename aside are the ones nothing will
    // mention again, and they are exactly as large as what was replaced.
    sweepAsides(installDir);

    if (!isStaged())
        return {};
    return applyFrom(stageDirectory(), installDir);
}

FileUpdatePackage::ApplyResult FileUpdatePackage::applyFrom(const QString &stageDir,
                                                            const QString &installDir)
{
    ApplyResult result;

    // Nothing staged is the normal case on almost every start, not a failure:
    // the caller is main(), which runs this before it knows anything.
    if (!QFileInfo::exists(planPath(stageDir)))
        return result;

    FileUpdate pkg;
    QString err;
    if (!readPlan(stageDir, &pkg, &err)) {
        result.error = err;
        return result;
    }
    result.version = pkg.version;

    QDir install(installDir);
    if (!install.exists()) {
        result.error = QStringLiteral("找不到程序目录 %1").arg(installDir);
        return result;
    }

    // Whatever the last run had to rename aside is free now, in the normal
    // case, and clearing it first means a small install directory does not
    // accumulate a copy of every Qt DLL every time somebody updates.
    sweepAsides(installDir);

    for (const FileUpdateEntry &e : pkg.files) {
        const QString target = install.filePath(e.path);
        const QString staged = QDir(stageDir).filePath(e.path);
        if (!QFileInfo::exists(staged)) {
            result.failed << e.path;
            continue;
        }
        if (replaceOne(staged, target, e.sha256))
            result.replaced << e.path;
        else
            result.failed << e.path;
    }

    for (const QString &rel : std::as_const(pkg.remove)) {
        if (!removeOne(install.filePath(rel)))
            result.failed << rel;
    }

    result.applied = !result.replaced.isEmpty();

    if (result.failed.isEmpty()) {
        result.complete = true;
        removeTree(stageDir);   // takes the error note with it
        s_applied = result.applied;
        s_appliedVersion = pkg.version;
        sweepAsides(installDir);
    } else {
        // Keep only what is still outstanding: a second start retries exactly
        // that and nothing else.
        FileUpdate remaining = pkg;
        remaining.files.clear();
        remaining.remove.clear();
        for (const FileUpdateEntry &e : pkg.files) {
            if (result.failed.contains(e.path))
                remaining.files << e;
        }
        for (const QString &rel : std::as_const(pkg.remove)) {
            if (result.failed.contains(rel))
                remaining.remove << rel;
        }
        writePlan(stageDir, remaining, nullptr);

        // Left behind so the next start can tell "not attempted yet" from
        // "attempted and something would not let go". Without it the updater
        // would keep offering a package that cannot land, with no way out
        // short of finding the installer by hand.
        QFile note(QDir(stageDir).filePath(QStringLiteral("last-error.txt")));
        if (note.open(QIODevice::WriteOnly | QIODevice::Truncate))
            note.write(result.failed.join(QStringLiteral(", ")).toUtf8());

        result.error = QStringLiteral("%1 个文件正在被占用,下次启动会再试一次")
                           .arg(result.failed.size());
    }

    return result;
}

bool FileUpdatePackage::writeFromDirectory(const QString &containerPath,
                                           const QString &version, const QString &base,
                                           const QStringList &relativePaths,
                                           const QStringList &remove,
                                           const QString &sourceDir,
                                           QString *error)
{
    const QDir source(sourceDir);

    QVector<FileUpdateEntry> entries;
    for (const QString &rel : relativePaths) {
        if (!isSafeRelativePath(rel)) {
            if (error) *error = QStringLiteral("路径不合法:%1").arg(rel);
            return false;
        }
        const QString path = source.filePath(rel);
        QFileInfo info(path);
        if (!info.isFile()) {
            if (error) *error = QStringLiteral("找不到 %1").arg(path);
            return false;
        }
        bool ok = false;
        const QByteArray sha = hashFile(path, &ok);
        if (!ok) {
            if (error) *error = QStringLiteral("读不了 %1").arg(path);
            return false;
        }
        entries.push_back({ rel, info.size(), QString::fromLatin1(sha) });
    }

    QJsonArray files;
    for (const FileUpdateEntry &e : entries)
        files.append(entryToJson(e));

    QJsonObject root{
        { QStringLiteral("version"), version },
        { QStringLiteral("base"), base },
        { QStringLiteral("files"), files },
        { QStringLiteral("remove"), QJsonArray::fromStringList(remove) },
    };
    const QByteArray header = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QSaveFile out(containerPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = out.errorString();
        return false;
    }
    out.write(kMagic, kMagicLen);
    out.write(writeBigEndian(quint32(header.size())));
    out.write(header);
    for (const FileUpdateEntry &e : entries) {
        QFile f(source.filePath(e.path));
        if (!f.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("读不了 %1").arg(f.fileName());
            return false;
        }
        while (!f.atEnd()) {
            const QByteArray chunk = f.read(kChunk);
            if (out.write(chunk) != chunk.size()) {
                if (error) *error = out.errorString();
                return false;
            }
        }
    }
    if (!out.commit()) {
        if (error) *error = out.errorString();
        return false;
    }
    return true;
}

} // namespace dreamdsp
