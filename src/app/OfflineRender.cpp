#include "app/OfflineRender.h"

#include "Compressor.h"
#include "Reverb.h"
#include "WavFile.h"

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
    bool useCompressor = false;
    dsp::Compressor::Params comp;
    bool useReverb = false;
    dsp::Reverb::Params rev;
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

    // Compressor first, then reverb -- compressing a reverb tail pumps it.
    if (job.useCompressor) {
        dsp::Compressor c;
        c.prepare(wav.sampleRate, wav.channelCount());
        c.setParams(job.comp);
        c.reset();
        c.process(buf);
    }
    if (job.useReverb) {
        dsp::Reverb rv;
        rv.prepare(wav.sampleRate);
        rv.setParams(job.rev);
        rv.reset();
        rv.process(buf);
    }

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

    const bool wantComp = m_compressor && m_compressor->enabled();
    const bool wantRev = m_reverb && m_reverb->enabled();
    if (!wantComp && !wantRev) {
        setStatus(QStringLiteral("没有启用任何效果 —— 先打开压缩器或混响"), true);
        return;
    }

    Job job;
    job.input = input;
    job.output = info.dir().filePath(info.completeBaseName() + QStringLiteral("-dreamdsp.wav"));
    job.useCompressor = wantComp;
    job.useReverb = wantRev;
    if (m_compressor) job.comp = m_compressor->dspParams();
    if (m_reverb)     job.rev = m_reverb->dspParams();

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
