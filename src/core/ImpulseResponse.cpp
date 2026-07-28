#include "core/ImpulseResponse.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace dreamdsp {

namespace {

quint32 le32(const uchar *p) { return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24); }
quint16 le16(const uchar *p) { return quint16(p[0]) | (quint16(p[1]) << 8); }

const QStringList kExtensions = {
    QStringLiteral("*.wav"), QStringLiteral("*.irs"),
    QStringLiteral("*.flac"), QStringLiteral("*.ogg"),
};

} // namespace

bool readWaveHeader(const QString &path, ImpulseResponse *out)
{
    if (!out)
        return false;

    QFileInfo info(path);
    out->path = QDir::toNativeSeparators(info.absoluteFilePath());
    out->name = info.completeBaseName();
    out->category = info.dir().dirName();
    out->readable = false;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    // 4 KB is plenty: fmt is almost always the first chunk, and we stop at data.
    const QByteArray head = f.read(4096);
    if (head.size() < 44)
        return false;

    const auto *p = reinterpret_cast<const uchar *>(head.constData());
    if (qstrncmp(head.constData(), "RIFF", 4) != 0
        || qstrncmp(head.constData() + 8, "WAVE", 4) != 0) {
        return false;   // flac/ogg: still usable by APO, just not inspectable here
    }

    int bytesPerFrame = 0;
    int pos = 12;
    while (pos + 8 <= head.size()) {
        const QByteArray id = head.mid(pos, 4);
        const quint32 size = le32(p + pos + 4);

        if (id == "fmt " && pos + 8 + 16 <= head.size()) {
            const uchar *fmt = p + pos + 8;
            out->channels = le16(fmt + 2);
            out->sampleRate = int(le32(fmt + 4));
            out->bitsPerSample = le16(fmt + 14);
            bytesPerFrame = out->channels * (out->bitsPerSample / 8);
        } else if (id == "data") {
            if (bytesPerFrame > 0)
                out->frames = qint64(size) / bytesPerFrame;
            break;
        }

        pos += 8 + int(size) + (int(size) & 1);   // chunks are word-aligned
        if (size == 0)
            break;
    }

    out->readable = (out->sampleRate > 0 && out->channels > 0);
    return out->readable;
}

QVector<ImpulseResponse> scanImpulseResponses(const QStringList &roots, int limit)
{
    QVector<ImpulseResponse> out;
    QStringList seen;

    for (const QString &root : roots) {
        if (root.isEmpty() || !QDir(root).exists())
            continue;

        QDirIterator it(root, kExtensions, QDir::Files,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext() && out.size() < limit) {
            const QString path = it.next();
            const QString key = QDir::toNativeSeparators(path).toLower();
            if (seen.contains(key))
                continue;
            seen << key;

            ImpulseResponse ir;
            // A failed header read still yields a usable entry: APO can open
            // formats this cannot inspect.
            readWaveHeader(path, &ir);
            out.push_back(ir);
        }
    }

    std::sort(out.begin(), out.end(), [](const ImpulseResponse &a, const ImpulseResponse &b) {
        if (a.category != b.category)
            return a.category.localeAwareCompare(b.category) < 0;
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

QStringList defaultImpulseRoots(const QString &apoConfigPath, const QString &userDir)
{
    QStringList roots;
    if (!userDir.isEmpty())
        roots << QDir(userDir).filePath(QStringLiteral("impulses"));
    if (!apoConfigPath.isEmpty())
        roots << apoConfigPath;

    // ViPER4Windows ships ~670 impulse responses; if it is installed, they are
    // the most useful library on the machine whether or not ViPER itself is
    // enabled on any device.
    for (const QString &base : { QStringLiteral("C:/Program Files/ViPER4Windows"),
                                 QStringLiteral("C:/Program Files (x86)/ViPER4Windows") }) {
        const QString dir = base + QStringLiteral("/ImpulseResponse");
        if (QDir(dir).exists())
            roots << dir;
    }
    return roots;
}

} // namespace dreamdsp
