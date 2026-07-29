#include "app/OfflineRender.h"

#include "EffectChain.h"
#include "ParamBlock.h"
#include "WavFile.h"

#include "app/ParamPublisher.h"   // blockFromModels

#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

namespace dreamdsp {

namespace {

struct Job {
    QString input;
    QString output;

    // The same struct that travels to audiodg. What you audition here is what
    // the system output will do, because both sides run dsp::EffectChain over
    // this block rather than each open-coding the rack.
    dsp::ParamBlock params = dsp::transparentBlock();

    bool anything() const { return params.enableMask != 0u; }
};

struct Result {
    bool ok = false;
    QString message;
    QString output;
};

// Runs off the GUI thread: everything it needs is copied into `job` first, so
// it never touches a QObject owned by another thread.
Result renderJob(Job job)
{
    Result r;
    r.output = job.output;

    dsp::WavData wav;
    std::string err;
    if (!dsp::readWav(job.input.toStdString(), &wav, &err)) {
        r.message = QStringLiteral("读取失败: %1").arg(QString::fromStdString(err));
        return r;
    }
    if (wav.frames() <= 0 || wav.channelCount() <= 0) {
        r.message = QStringLiteral("文件里没有音频");
        return r;
    }

    QElapsedTimer timer;
    timer.start();

    std::vector<float *> ch;
    ch.reserve(size_t(wav.channelCount()));
    for (auto &v : wav.channels)
        ch.push_back(v.data());
    dsp::AudioBuffer buf{ ch.data(), wav.channelCount(), wav.frames() };

    // One chain, defined once, in dsp::EffectChain -- including the order the
    // effects run in. This used to be an open-coded if-chain here, which meant
    // the preview and the copy inside audiodg were two implementations that
    // could drift apart without anything failing.
    const int channels = wav.channelCount();
    const double sr = wav.sampleRate;

    dsp::EffectChain chain;
    chain.prepare(sr, channels, wav.frames());
    chain.apply(job.params);
    chain.process(buf);

    const qint64 ms = timer.elapsed();

    if (!dsp::writeWav(job.output.toStdString(), wav, &err)) {
        r.message = QStringLiteral("写入失败: %1").arg(QString::fromStdString(err));
        return r;
    }

    const double seconds = double(wav.frames()) / double(wav.sampleRate ? wav.sampleRate : 1);
    r.ok = true;
    r.message = QStringLiteral("已处理 %1 秒音频,用时 %2 ms → %3")
                    .arg(seconds, 0, 'f', 1)
                    .arg(ms)
                    .arg(QFileInfo(job.output).fileName());
    return r;
}

} // namespace

OfflineRender::OfflineRender(QObject *parent)
    : QObject(parent)
{
}

void OfflineRender::setStatus(const QString &s, bool failed)
{
    m_status = s;
    m_failed = failed;
    emit statusChanged();
}

void OfflineRender::setBusy(bool b)
{
    if (m_busy == b)
        return;
    m_busy = b;
    emit busyChanged();
}

void OfflineRender::renderUrl(const QUrl &url)
{
    if (m_busy)
        return;

    const QString input = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFileInfo info(input);
    if (!info.exists() || !info.isFile()) {
        setStatus(QStringLiteral("找不到文件"), true);
        return;
    }

    Job job;
    job.input = input;
    job.output = info.dir().filePath(info.completeBaseName() + QStringLiteral("-dreamdsp.wav"));

    // Built by the same function the publisher uses, so the preview cannot be
    // assembled differently from what is sent to audiodg.
    job.params = blockFromModels(m_compressor, m_reverb, m_effects);

    if (!job.anything()) {
        setStatus(QStringLiteral("没有启用任何效果 —— 先打开一个"), true);
        return;
    }

    setBusy(true);
    setStatus(QStringLiteral("正在处理 %1 …").arg(info.fileName()), false);

    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher] {
        const Result r = watcher->result();
        m_lastOutput = r.ok ? r.output : QString();
        setStatus(r.message, !r.ok);
        setBusy(false);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(renderJob, job));
}

void OfflineRender::revealOutput() const
{
    if (m_lastOutput.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastOutput).absolutePath()));
}

} // namespace dreamdsp
