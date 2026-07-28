#include "app/OfflineRender.h"

#include "Compressor.h"
#include "MultibandCompressor.h"
#include "Reverb.h"
#include "Saturation.h"
#include "Stereo.h"
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

    bool useBass = false;      dsp::VirtualBass::Params bass;
    bool useExciter = false;   dsp::Exciter::Params exciter;
    bool useTube = false;      dsp::TubeStage::Params tube;
    bool useCompressor = false; dsp::Compressor::Params comp;
    bool useMultiband = false; dsp::MultibandCompressor::Params multiband;
    bool useReverb = false;    dsp::Reverb::Params rev;
    bool useWidth = false;     dsp::StereoWidener::Params width;
    bool useCrossfeed = false; dsp::Crossfeed::Params crossfeed;

    bool anything() const
    {
        return useBass || useExciter || useTube || useCompressor
               || useMultiband || useReverb || useWidth || useCrossfeed;
    }
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

    // Chain order, and why:
    //   bass and exciter first -- they add content the later stages should see
    //   tube next, colouring the whole spectrum
    //   dynamics after tone, so the compressor reacts to the finished sound
    //   reverb after dynamics, because compressing a tail pumps it
    //   width and crossfeed last: they are output-stage, not part of the tone
    const int channels = wav.channelCount();
    const double sr = wav.sampleRate;

    if (job.useBass) {
        dsp::VirtualBass e; e.prepare(sr, channels); e.setParams(job.bass); e.reset(); e.process(buf);
    }
    if (job.useExciter) {
        dsp::Exciter e; e.prepare(sr, channels); e.setParams(job.exciter); e.reset(); e.process(buf);
    }
    if (job.useTube) {
        dsp::TubeStage e; e.prepare(sr, channels); e.setParams(job.tube); e.reset(); e.process(buf);
    }
    if (job.useCompressor) {
        dsp::Compressor c; c.prepare(sr, channels); c.setParams(job.comp); c.reset(); c.process(buf);
    }
    if (job.useMultiband) {
        dsp::MultibandCompressor m; m.prepare(sr, channels); m.setParams(job.multiband);
        m.reset(); m.process(buf);
    }
    if (job.useReverb) {
        dsp::Reverb rv; rv.prepare(sr); rv.setParams(job.rev); rv.reset(); rv.process(buf);
    }
    if (job.useWidth) {
        dsp::StereoWidener w; w.prepare(sr); w.setParams(job.width); w.reset(); w.process(buf);
    }
    if (job.useCrossfeed) {
        dsp::Crossfeed cf; cf.prepare(sr); cf.setParams(job.crossfeed); cf.reset(); cf.process(buf);
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

    Job job;
    job.input = input;
    job.output = info.dir().filePath(info.completeBaseName() + QStringLiteral("-dreamdsp.wav"));

    if (m_compressor && m_compressor->enabled()) {
        job.useCompressor = true;
        job.comp = m_compressor->dspParams();
    }
    if (m_reverb && m_reverb->enabled()) {
        job.useReverb = true;
        job.rev = m_reverb->dspParams();
    }
    if (m_effects) {
        job.useBass = m_effects->bassOn();           job.bass = m_effects->bassParams();
        job.useExciter = m_effects->exciterOn();     job.exciter = m_effects->exciterParams();
        job.useTube = m_effects->tubeOn();           job.tube = m_effects->tubeParams();
        job.useMultiband = m_effects->multibandOn(); job.multiband = m_effects->multibandParams();
        job.useWidth = m_effects->widthOn();         job.width = m_effects->widthParams();
        job.useCrossfeed = m_effects->crossfeedOn(); job.crossfeed = m_effects->crossfeedParams();
    }

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
