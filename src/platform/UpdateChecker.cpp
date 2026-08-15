#include "platform/UpdateChecker.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace dreamdsp {

namespace {

const char *kRepo = DREAMDSP_REPO;

// The only hosts this will talk to. GitHub serves release assets from a
// different domain than the API, and both have to be named -- but nothing
// else does, so a redirect to anywhere else is a refusal rather than a hop.
bool hostAllowed(const QUrl &url)
{
    if (url.scheme() != QLatin1String("https"))
        return false;
    const QString host = url.host().toLower();
    return host == QLatin1String("api.github.com")
           || host == QLatin1String("github.com")
           || host == QLatin1String("objects.githubusercontent.com")
           || host == QLatin1String("release-assets.githubusercontent.com");
}

QString downloadDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + QStringLiteral("/DreamDSP-update");
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    QSettings s(QStringLiteral("DreamDSP"), QStringLiteral("DreamDSP"));
    m_automatic = s.value(QStringLiteral("checkForUpdates"), true).toBool();
}

UpdateChecker::~UpdateChecker() = default;

QString UpdateChecker::currentVersion() const
{
    return QString::fromLatin1(DREAMDSP_VERSION_FULL);
}

void UpdateChecker::setAutomatic(bool on)
{
    if (m_automatic == on)
        return;
    m_automatic = on;
    QSettings s(QStringLiteral("DreamDSP"), QStringLiteral("DreamDSP"));
    s.setValue(QStringLiteral("checkForUpdates"), on);
    emit automaticChanged();
}

void UpdateChecker::setStatus(const QString &s)
{
    m_status = s;
    emit stateChanged();
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    // Compared as numbers, one component at a time. A string comparison would
    // put 0.10.0 before 0.9.0 and strand every installation at 0.9.
    const auto parts = [](const QString &v) {
        QList<int> out;
        // Anything after the numbers -- a build suffix, a pre-release tag -- is
        // deliberately ignored: it does not order releases.
        const QStringList bits = v.section(QLatin1Char('+'), 0, 0)
                                     .section(QLatin1Char('-'), 0, 0)
                                     .split(QLatin1Char('.'));
        for (const QString &bit : bits)
            out.append(bit.toInt());
        while (out.size() < 3)
            out.append(0);
        return out;
    };

    const QList<int> x = parts(a);
    const QList<int> y = parts(b);
    for (int i = 0; i < 3; ++i) {
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

void UpdateChecker::checkNow()
{
    if (m_busy)
        return;

    m_busy = true;
    m_percent = 0;
    setStatus(QStringLiteral("正在检查更新…"));

    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                                     .arg(QString::fromLatin1(kRepo))));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DreamDSP/%1").arg(QString::fromLatin1(DREAMDSP_VERSION)));
    // Redirects are followed only within the allowed hosts, checked below.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        onCheckFinished(reply);
        reply->deleteLater();
    });
}

void UpdateChecker::onCheckFinished(QNetworkReply *reply)
{
    m_busy = false;

    if (reply->error() != QNetworkReply::NoError) {
        setStatus(QStringLiteral("检查失败:%1").arg(reply->errorString()));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        setStatus(QStringLiteral("检查失败:返回内容无法解析"));
        return;
    }

    const QJsonObject release = doc.object();
    if (release.value(QStringLiteral("draft")).toBool()
        || release.value(QStringLiteral("prerelease")).toBool()) {
        setStatus(QStringLiteral("最新的是草稿或预发布版本,已跳过"));
        return;
    }

    QString tag = release.value(QStringLiteral("tag_name")).toString();
    if (tag.startsWith(QLatin1Char('v')))
        tag.remove(0, 1);
    if (tag.isEmpty()) {
        setStatus(QStringLiteral("检查失败:发布没有版本号"));
        return;
    }

    m_latestVersion = tag;
    m_releaseNotes = release.value(QStringLiteral("body")).toString();
    m_releaseUrl = release.value(QStringLiteral("html_url")).toString();

    // The installer, and the SHA-256 the build published for it. Both have to
    // be present -- an asset with no checksum cannot be verified, so it is not
    // offered rather than being installed on trust.
    m_assetUrl.clear();
    m_assetName.clear();
    m_expectedSha256.clear();
    m_assetSize = 0;

    for (const QJsonValue &v : release.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = v.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
            continue;
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        if (!hostAllowed(url))
            continue;
        m_assetUrl = url.toString();
        m_assetName = name;
        m_assetSize = qint64(asset.value(QStringLiteral("size")).toDouble());
        break;
    }

    // Written into the release notes by the workflow, as
    // `SHA-256: <64 hex> <filename>`.
    if (!m_assetName.isEmpty()) {
        const QRegularExpression re(
            QStringLiteral("SHA-?256[:\\s]+([0-9a-fA-F]{64})"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = re.match(m_releaseNotes);
        if (m.hasMatch())
            m_expectedSha256 = m.captured(1).toLower();
    }

    const int cmp = compareVersions(QString::fromLatin1(DREAMDSP_VERSION), m_latestVersion);
    m_updateAvailable = cmp < 0;

    if (!m_updateAvailable) {
        setStatus(QStringLiteral("已是最新版本(%1)").arg(QString::fromLatin1(DREAMDSP_VERSION)));
    } else if (m_assetUrl.isEmpty()) {
        setStatus(QStringLiteral("有新版本 %1,但这个发布没有安装包").arg(m_latestVersion));
    } else if (m_expectedSha256.isEmpty()) {
        // Deliberately not offered. Downloading and running an installer whose
        // contents cannot be checked is exactly the thing this design refuses
        // to do, however convenient it would be.
        m_updateAvailable = false;
        setStatus(QStringLiteral("有新版本 %1,但发布说明里没有 SHA-256 校验值,"
                                 "不会自动下载 —— 请到发布页手动获取")
                      .arg(m_latestVersion));
    } else {
        setStatus(QStringLiteral("有新版本 %1 可用").arg(m_latestVersion));
    }
    emit stateChanged();
}

void UpdateChecker::download()
{
    if (m_busy || m_assetUrl.isEmpty() || m_expectedSha256.isEmpty())
        return;

    const QUrl url(m_assetUrl);
    if (!hostAllowed(url)) {
        setStatus(QStringLiteral("下载地址不在允许的域名内,已拒绝"));
        return;
    }

    m_busy = true;
    m_percent = 0;
    m_downloadedPath.clear();
    setStatus(QStringLiteral("正在下载 %1…").arg(m_assetName));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DreamDSP/%1").arg(QString::fromLatin1(DREAMDSP_VERSION)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_network->get(request);

    connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl &to) {
        if (!hostAllowed(to)) {
            setStatus(QStringLiteral("下载被重定向到 %1,已中止").arg(to.host()));
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 got, qint64 total) {
                m_percent = (total > 0) ? int(got * 100 / total) : 0;
                emit progressChanged();
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        onDownloadFinished(reply);
        reply->deleteLater();
    });
}

void UpdateChecker::onDownloadFinished(QNetworkReply *reply)
{
    m_busy = false;

    if (reply->error() != QNetworkReply::NoError) {
        setStatus(QStringLiteral("下载失败:%1").arg(reply->errorString()));
        return;
    }

    const QByteArray payload = reply->readAll();
    if (payload.isEmpty()) {
        setStatus(QStringLiteral("下载失败:文件是空的"));
        return;
    }

    const QString actual =
        QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    if (actual != m_expectedSha256) {
        // A hard failure with nothing left on disk. A mismatch means the file
        // is not the one the build produced, and there is no reading of that
        // which makes running it acceptable.
        setStatus(QStringLiteral("校验失败 —— 下载的文件与发布公布的 SHA-256 不符,已丢弃"));
        return;
    }

    QDir().mkpath(downloadDirectory());
    const QString path = downloadDirectory() + QLatin1Char('/') + m_assetName;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setStatus(QStringLiteral("无法写入 %1").arg(path));
        return;
    }
    file.write(payload);
    file.close();

    m_downloadedPath = path;
    setStatus(QStringLiteral("%1 已下载并校验通过,可以安装").arg(m_assetName));
}

void UpdateChecker::installNow()
{
    if (m_downloadedPath.isEmpty() || !QFile::exists(m_downloadedPath)) {
        setStatus(QStringLiteral("还没有已下载的安装包"));
        return;
    }

    // /SILENT rather than /VERYSILENT: the progress window is worth seeing,
    // because the installer stops the audio service to replace the processing
    // object and the machine goes quiet for a moment.
    const bool started = QProcess::startDetached(
        m_downloadedPath, { QStringLiteral("/SILENT"), QStringLiteral("/NOCANCEL") });
    if (!started) {
        setStatus(QStringLiteral("无法启动安装程序"));
        return;
    }

    setStatus(QStringLiteral("安装程序已启动,DreamDSP 即将退出"));
    // The installer cannot replace files this process has open.
    emit quitRequested();
}

void UpdateChecker::openReleasePage() const
{
    const QUrl url(m_releaseUrl.isEmpty()
                       ? QStringLiteral("https://github.com/%1/releases")
                             .arg(QString::fromLatin1(kRepo))
                       : m_releaseUrl);
    if (hostAllowed(url))
        QDesktopServices::openUrl(url);
}

} // namespace dreamdsp
