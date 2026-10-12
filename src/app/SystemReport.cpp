// The "系统音频" page's data: everything that has to be true for a slider to
// change what comes out of the speakers, checked in order, with the first one
// that fails turned into a sentence and a button.
//
// It exists because the failure modes here are all silent. A driver update
// that moves an endpoint to a composite effect chain, an uninstaller that
// switches off "all enhancements", an audio service that has not been
// restarted since an attach -- every one of them leaves the registration
// intact, the DLL loadable, and the interface saying "attached", and the only
// symptom is that nothing happens. Each of those was found once by hand, with
// a registry editor and a log file; this is that procedure, run every two
// seconds while the page is open.

#include "app/AppController.h"

#include "ParamBlock.h"
#include "StatusBlock.h"
#include "platform/ApoInstaller.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

#include <windows.h>
#include <tlhelp32.h>

namespace dreamdsp {

namespace {

// When the audio engine's process last started, or invalid if it is not
// running or cannot be queried. PROCESS_QUERY_LIMITED_INFORMATION is enough to
// read the creation time of a LOCAL SERVICE process without elevation.
QDateTime audiodgStartTime()
{
    QDateTime when;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return when;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    for (BOOL ok = ::Process32FirstW(snap, &pe); ok; ok = ::Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"audiodg.exe") != 0)
            continue;
        HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!proc)
            continue;
        FILETIME created{}, exited{}, kernel{}, user{};
        if (::GetProcessTimes(proc, &created, &exited, &kernel, &user)) {
            const qint64 t = (qint64(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            // FILETIME is 100 ns ticks since 1601; the offset to the Unix epoch
            // is 11644473600 seconds.
            when = QDateTime::fromMSecsSinceEpoch(t / 10000 - 11644473600000LL);
        }
        ::CloseHandle(proc);
        break;
    }
    ::CloseHandle(snap);
    return when;
}

// The last few lines of the processing object's own trace. Read tolerantly:
// it is appended to by a process we do not control, and has in the past ended
// up with stray bytes in it, which must not make the page blank.
QStringList logTail(int lines)
{
    QFile f(QStringLiteral("C:/ProgramData/DreamDSP/apo.log"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = f.size();
    f.seek(std::max<qint64>(0, size - 8192));
    QByteArray raw = f.readAll();
    raw.replace('\0', "");
    QStringList all = QString::fromUtf8(raw).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (size > 8192 && !all.isEmpty())
        all.removeFirst();            // probably cut mid-line by the seek
    QStringList out;
    for (QString l : all) {
        l = l.trimmed();
        if (!l.isEmpty())
            out << l;
    }
    return out.mid(std::max<qsizetype>(0, out.size() - lines));
}

QString effectNames(uint32_t mask)
{
    static const struct { uint32_t bit; const char *name; } kNames[] = {
        { dsp::kEnEqualizer, "参数均衡" },  { dsp::kEnGraphic, "图形均衡" },
        { dsp::kEnLoudness, "等响度" },     { dsp::kEnDynBass, "动态低音" },
        { dsp::kEnBass, "虚拟低音" },       { dsp::kEnExciter, "激励器" },
        { dsp::kEnTube, "电子管" },         { dsp::kEnTransient, "瞬态整形" },
        { dsp::kEnComp, "压缩器" },         { dsp::kEnMultiband, "多段压缩" },
        { dsp::kEnNightMode, "夜间模式" },  { dsp::kEnReverb, "混响" },
        { dsp::kEnWidth, "声场展宽" },      { dsp::kEnCrossfeed, "串扰" },
        { dsp::kEnMatrix, "声道路由" },     { dsp::kEnConvolution, "卷积" },
        { dsp::kEnDelay, "声道延时" },      { dsp::kEnAutoGain, "自动音量" },
        { dsp::kEnClipper, "削波器" },      { dsp::kEnLimiter, "限制器" },
    };
    QStringList names;
    for (const auto &n : kNames)
        if (mask & n.bit)
            names << QString::fromUtf8(n.name);
    return names.isEmpty() ? QStringLiteral("(无)") : names.join(QStringLiteral(" · "));
}

QVariantMap check(const QString &label, const QString &state, const QString &detail,
                  const QString &action = {}, const QString &actionText = {})
{
    return QVariantMap{
        { QStringLiteral("label"), label },   { QStringLiteral("state"), state },
        { QStringLiteral("detail"), detail }, { QStringLiteral("action"), action },
        { QStringLiteral("actionText"), actionText },
    };
}

QVariantList stagesToVariant(const EndpointChain &ch)
{
    QVariantList stages;
    for (const ChainStage &st : ch.stages) {
        QVariantList entries;
        for (const ChainEntry &e : st.entries) {
            entries << QVariantMap{
                { QStringLiteral("name"), e.name.isEmpty() ? e.clsid : e.name },
                { QStringLiteral("clsid"), e.clsid },
                { QStringLiteral("dll"), QFileInfo(e.dll).fileName() },
                { QStringLiteral("dllPath"), e.dll },
                { QStringLiteral("ours"), e.isOurs },
            };
        }
        stages << QVariantMap{
            { QStringLiteral("key"), st.key },
            { QStringLiteral("label"), st.label },
            { QStringLiteral("ignored"), st.ignored },
            { QStringLiteral("entries"), entries },
        };
    }
    return stages;
}

} // namespace

QVariantMap AppController::systemReport()
{
    // Re-read the machine-wide state rather than trusting the cached copy:
    // this page is where someone comes after changing something outside the
    // application, and a stale answer here is worse than none.
    m_apoState = apoState();

    QVariantMap r;
    const QString targetId = apoTargetDevice();
    const EndpointChain chain = endpointChain(targetId);

    QString targetName;
    for (const AudioDevice &d : m_devices)
        if (d.id == targetId)
            targetName = d.name;

    // ------------------------------------------------------------ runtime
    const QFileInfo statusFile(QStringLiteral("C:/ProgramData/DreamDSP/status.bin"));
    const QDateTime statusTime = statusFile.exists() ? statusFile.lastModified() : QDateTime();
    const QDateTime dgStart = audiodgStartTime();
    const bool fresh = m_publisher.statusFresh();
    const dsp::StatusBlock &s = m_publisher.status();

    // "Loaded since the audio service last started" is the question that
    // separates a chain Windows reads from one it does not. The status file is
    // only ever written by the processing object itself, so a status file
    // older than the audio engine's process means nothing has loaded us since.
    const bool knowLoad = dgStart.isValid();
    const bool loadedThisSession = knowLoad && statusTime.isValid() && statusTime >= dgStart;

    uint32_t appliedMask = 0;
    QVariantList instances;
    int streaming = 0;
    for (uint32_t i = 0; i < s.instanceCount && i < 8; ++i) {
        const dsp::StatusInstance &in = s.inst[i];
        appliedMask |= in.appliedMask;
        const bool live = (in.flags & dsp::kSfStreaming) != 0;
        if (live)
            ++streaming;
        instances << QVariantMap{
            { QStringLiteral("rate"), int(in.sampleRate) },
            { QStringLiteral("channels"), int(in.channels) },
            { QStringLiteral("frames"), int(in.maxFrames) },
            { QStringLiteral("generation"), int(in.appliedGeneration) },
            { QStringLiteral("effects"), effectNames(in.appliedMask) },
            { QStringLiteral("streaming"), live },
            { QStringLiteral("passthrough"), (in.flags & dsp::kSfPassthrough) != 0 },
            { QStringLiteral("seconds"), in.sampleRate
                  ? double(in.framesProcessed) / double(in.sampleRate) : 0.0 },
        };
    }

    int enabledCount = 0;
    QStringList enabledNames;
    for (const QVariant &v : this->chain()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("working")).toBool()) {
            ++enabledCount;
            enabledNames << m.value(QStringLiteral("name")).toString();
        }
    }

    QVariantMap runtime{
        { QStringLiteral("fresh"), fresh },
        { QStringLiteral("statusTime"), statusTime.isValid()
              ? statusTime.toString(QStringLiteral("MM-dd HH:mm:ss")) : QStringLiteral("从未") },
        { QStringLiteral("audiodgStart"), dgStart.isValid()
              ? dgStart.toString(QStringLiteral("MM-dd HH:mm:ss")) : QStringLiteral("未运行") },
        { QStringLiteral("loadedThisSession"), loadedThisSession },
        { QStringLiteral("loadedGeneration"), int(s.loadedGeneration) },
        { QStringLiteral("publishedGeneration"), int(m_publisher.generation()) },
        { QStringLiteral("loads"), int(s.loadCount) },
        { QStringLiteral("rejects"), int(s.rejectCount) },
        { QStringLiteral("lastError"), int(s.lastError) },
        { QStringLiteral("instances"), instances },
        { QStringLiteral("streaming"), streaming },
        { QStringLiteral("running"), effectNames(appliedMask) },
    };
    r.insert(QStringLiteral("runtime"), runtime);
    r.insert(QStringLiteral("log"), logTail(14));

    // ------------------------------------------------------------- device
    QString driverEffects;
    for (const ChainStage &st : chain.stages) {
        if (st.ignored)
            continue;
        for (const ChainEntry &e : st.entries)
            if (!e.isOurs)
                driverEffects = e.dll.isEmpty() ? e.name : QFileInfo(e.dll).fileName();
    }
    r.insert(QStringLiteral("device"), QVariantMap{
        { QStringLiteral("id"), targetId },
        { QStringLiteral("name"), targetName.isEmpty() ? QStringLiteral("(没有设备)") : targetName },
        { QStringLiteral("composite"), chain.composite },
        { QStringLiteral("sysFxDisabled"), chain.sysFxDisabled },
        { QStringLiteral("attached"), chain.oursPresent },
        { QStringLiteral("inert"), chain.oursInert },
        { QStringLiteral("stages"), stagesToVariant(chain) },
    });

    QVariantList endpoints;
    for (const AudioDevice &d : m_devices) {
        if (!d.active)
            continue;
        const EndpointChain ec = endpointChain(d.id);
        int others = 0;
        for (const ChainStage &st : ec.stages)
            if (!st.ignored)
                for (const ChainEntry &e : st.entries)
                    if (!e.isOurs)
                        ++others;
        endpoints << QVariantMap{
            { QStringLiteral("id"), d.id },
            { QStringLiteral("name"), d.name },
            { QStringLiteral("isDefault"), d.isDefault },
            { QStringLiteral("current"), d.id == targetId },
            { QStringLiteral("composite"), ec.composite },
            { QStringLiteral("attached"), ec.oursPresent },
            { QStringLiteral("inert"), ec.oursInert },
            { QStringLiteral("sysFxDisabled"), ec.sysFxDisabled },
            { QStringLiteral("others"), others },
        };
    }
    r.insert(QStringLiteral("endpoints"), endpoints);

    // ------------------------------------------------------------- checks
    const bool installed = m_apoState.installed();
    QVariantList checks;

    checks << (installed
        ? check(QStringLiteral("音频组件已安装"), QStringLiteral("ok"),
                QStringLiteral("DLL 在 ProgramData,CLSID 与音频引擎注册都在"))
        : check(QStringLiteral("音频组件已安装"), QStringLiteral("error"),
                !m_apoState.dllStaged ? QStringLiteral("DLL 不在 ProgramData")
                : !m_apoState.clsidRegistered ? QStringLiteral("CLSID 没有注册")
                : QStringLiteral("没有在音频引擎里注册 —— 少了这一项 Windows 会静默忽略它"),
                QStringLiteral("install"), QStringLiteral("安装")));

    if (installed) {
        checks << (m_apoState.upToDate
            ? check(QStringLiteral("组件与程序版本一致"), QStringLiteral("ok"),
                    QStringLiteral("ProgramData 里的副本就是这个程序带的那份"))
            : check(QStringLiteral("组件与程序版本一致"), QStringLiteral("warn"),
                    QStringLiteral("ProgramData 里的副本比程序旧 —— 参数格式变过时它会拒绝新参数"),
                    QStringLiteral("install"), QStringLiteral("重新安装")));
    }

    if (chain.oursPresent) {
        checks << check(QStringLiteral("已接入当前设备"), QStringLiteral("ok"),
                        chain.composite
                            ? QStringLiteral("位于复合效果链的 EFX 段,与驱动自带的音效共存")
                            : QStringLiteral("位于后混 (GFX) 槽位"));
    } else if (chain.oursInert) {
        checks << check(QStringLiteral("已接入当前设备"), QStringLiteral("error"),
                        QStringLiteral("DreamDSP 挂在一个会被忽略的旧式槽位里。这台设备的驱动装了"
                                       "复合效果链,Windows 只读复合链,不再读旧槽位 —— "
                                       "界面会显示已接入,实际一个字节也不会经过它"),
                        QStringLiteral("attach"), QStringLiteral("重新接入"));
    } else {
        checks << check(QStringLiteral("已接入当前设备"), QStringLiteral("warn"),
                        QStringLiteral("这台设备的效果链里没有 DreamDSP"),
                        QStringLiteral("attach"), QStringLiteral("接入"));
    }

    checks << (chain.sysFxDisabled
        ? check(QStringLiteral("设备的声音增强已开启"), QStringLiteral("error"),
                QStringLiteral("这台设备勾了「禁用所有声音增强」,Windows 不会加载任何音效对象"),
                QStringLiteral("attach"), QStringLiteral("开启"))
        : check(QStringLiteral("设备的声音增强已开启"), QStringLiteral("ok"),
                QStringLiteral("没有被「禁用所有声音增强」挡住")));

    if (m_apoRestartPending) {
        checks << check(QStringLiteral("改动已生效"), QStringLiteral("warn"),
                        QStringLiteral("效果链改过了,但音频服务还没重启 —— 效果链只在那时重建"),
                        QStringLiteral("restart"), QStringLiteral("重启音频服务"));
    }

    if (!knowLoad) {
        checks << check(QStringLiteral("Windows 已加载 DreamDSP"), QStringLiteral("info"),
                        QStringLiteral("读不到音频引擎进程的启动时间"));
    } else if (loadedThisSession && !chain.oursPresent) {
        // The status file is machine-wide: one write from any endpoint makes it
        // fresh. Reporting that as "loaded" while the device in question has
        // no DreamDSP in a chain Windows reads would be the same comfortable
        // lie the composite case told, one level up.
        checks << check(QStringLiteral("Windows 已加载 DreamDSP"), QStringLiteral("info"),
                        QStringLiteral("加载过,但是在别的设备上 —— 这台设备的效果链里没有它"));
    } else if (loadedThisSession) {
        checks << check(QStringLiteral("Windows 已加载 DreamDSP"), QStringLiteral("ok"),
                        QStringLiteral("音频服务 %1 启动后加载过,最近一次反馈 %2")
                            .arg(runtime.value(QStringLiteral("audiodgStart")).toString(),
                                 runtime.value(QStringLiteral("statusTime")).toString()));
    } else {
        checks << check(QStringLiteral("Windows 已加载 DreamDSP"), QStringLiteral("warn"),
                        QStringLiteral("音频服务 %1 启动以来一次都没有加载它。没有声音在播放时这是正常的 —— "
                                       "如果正在播放仍然如此,说明 Windows 没在读 DreamDSP 所在的那条链")
                            .arg(runtime.value(QStringLiteral("audiodgStart")).toString()),
                        QStringLiteral("restart"), QStringLiteral("重启音频服务"));
    }

    if (s.rejectCount > 0 && s.loadCount == 0) {
        checks << check(QStringLiteral("参数已送达"), QStringLiteral("error"),
                        QStringLiteral("参数文件被拒绝了 %1 次 —— 组件与程序的参数格式不一致")
                            .arg(s.rejectCount),
                        QStringLiteral("install"), QStringLiteral("重新安装"));
    } else if (fresh && s.loadedGeneration != m_publisher.generation()) {
        checks << check(QStringLiteral("参数已送达"), QStringLiteral("warn"),
                        QStringLiteral("组件在用第 %1 代,程序已发布第 %2 代")
                            .arg(s.loadedGeneration).arg(m_publisher.generation()));
    } else if (fresh) {
        checks << check(QStringLiteral("参数已送达"), QStringLiteral("ok"),
                        QStringLiteral("第 %1 代,组件已接收").arg(s.loadedGeneration));
    } else {
        checks << check(QStringLiteral("参数已送达"), QStringLiteral("info"),
                        QStringLiteral("当前没有音频流,没有可比对的反馈"));
    }

    checks << (m_masterEnabled
        ? check(QStringLiteral("总开关"), QStringLiteral("ok"), QStringLiteral("开"))
        : check(QStringLiteral("总开关"), QStringLiteral("warn"),
                QStringLiteral("关 —— 全部效果旁通,各自的开关与参数保留"),
                QStringLiteral("master"), QStringLiteral("打开")));

    checks << (enabledCount > 0
        ? check(QStringLiteral("启用的效果"), QStringLiteral("ok"),
                enabledNames.join(QStringLiteral(" · ")))
        : check(QStringLiteral("启用的效果"), QStringLiteral("info"),
                QStringLiteral("一个都没开,声音会原样通过")));
    r.insert(QStringLiteral("checks"), checks);

    // ------------------------------------------------------------ verdict
    // The first failing condition, in the order a signal meets them.
    QVariantMap v;
    const auto verdict = [&](const char *level, const QString &title, const QString &detail,
                             const QString &action = {}, const QString &actionText = {}) {
        v = QVariantMap{
            { QStringLiteral("level"), QString::fromLatin1(level) },
            { QStringLiteral("title"), title }, { QStringLiteral("detail"), detail },
            { QStringLiteral("action"), action }, { QStringLiteral("actionText"), actionText },
        };
    };

    if (!installed) {
        verdict("error", QStringLiteral("音频组件没有安装"),
                QStringLiteral("没有它 DreamDSP 无法处理系统声音。安装需要一次管理员授权。"),
                QStringLiteral("install"), QStringLiteral("安装音频组件"));
    } else if (chain.sysFxDisabled) {
        verdict("error", QStringLiteral("这台设备关闭了声音增强"),
                QStringLiteral("Windows 不会为它加载任何音效对象,包括 DreamDSP。"),
                QStringLiteral("attach"), QStringLiteral("开启并接入"));
    } else if (chain.oursInert) {
        verdict("error", QStringLiteral("DreamDSP 挂在一个被忽略的槽位里"),
                QStringLiteral("驱动给这台设备装了复合效果链(%1),Windows 只读复合链。"
                               "重新接入会把 DreamDSP 加进复合链,和驱动的音效共存。")
                    .arg(driverEffects.isEmpty() ? QStringLiteral("驱动自带") : driverEffects),
                QStringLiteral("attach"), QStringLiteral("重新接入"));
    } else if (!chain.oursPresent) {
        verdict("warn", QStringLiteral("没有接入「%1」").arg(targetName),
                QStringLiteral("接入后需要重启一次音频服务。"),
                QStringLiteral("attach"), QStringLiteral("接入这台设备"));
    } else if (m_apoRestartPending) {
        verdict("warn", QStringLiteral("改动还没生效"),
                QStringLiteral("效果链只在音频服务重启时重建,全机声音会中断一两秒。"),
                QStringLiteral("restart"), QStringLiteral("重启音频服务"));
    } else if (s.rejectCount > 0 && s.loadCount == 0) {
        verdict("error", QStringLiteral("组件拒绝了参数"),
                QStringLiteral("组件和程序的参数格式不一致 —— 升级后没有重装组件时就是这样。"),
                QStringLiteral("install"), QStringLiteral("重新安装组件"));
    } else if (!m_masterEnabled) {
        verdict("off", QStringLiteral("总开关已关闭"),
                QStringLiteral("所有效果都被旁通,声音原样通过。各效果的开关和参数都保留着。"),
                QStringLiteral("master"), QStringLiteral("打开总开关"));
    } else if (knowLoad && !loadedThisSession) {
        verdict("warn", QStringLiteral("还没有声音经过 DreamDSP"),
                QStringLiteral("音频服务启动后它还没被加载过。播放一点声音;如果正在播放仍然如此,重启音频服务。"),
                QStringLiteral("restart"), QStringLiteral("重启音频服务"));
    } else if (!fresh) {
        verdict("ok", QStringLiteral("已就绪"),
                QStringLiteral("已接入并加载过,当前没有声音在播放。"));
    } else if (enabledCount == 0) {
        verdict("ok", QStringLiteral("正在直通"),
                QStringLiteral("DreamDSP 在工作,但没有启用任何效果。"));
    } else if (s.loadedGeneration != m_publisher.generation()) {
        verdict("warn", QStringLiteral("参数正在同步"),
                QStringLiteral("组件在用第 %1 代,程序已发布第 %2 代。持续不变的话重装一次组件。")
                    .arg(s.loadedGeneration).arg(m_publisher.generation()),
                QStringLiteral("install"), QStringLiteral("重新安装组件"));
    } else {
        verdict("ok", QStringLiteral("正在生效"),
                QStringLiteral("%1 路音频流 · %2").arg(streaming).arg(effectNames(appliedMask)));
    }
    r.insert(QStringLiteral("verdict"), v);
    r.insert(QStringLiteral("master"), m_masterEnabled);
    return r;
}

void AppController::setEndpointAttached(const QString &endpointId, bool on)
{
    if (endpointId.isEmpty())
        return;
    const QString err = on ? attachApo(endpointId) : detachApo(endpointId);
    if (!err.isEmpty()) {
        setError(err);
        refreshApoState();
        return;
    }
    m_apoRestartPending = true;
    setMessage(on ? QStringLiteral("已接入 · 重启音频服务后生效")
                  : QStringLiteral("已移除 · 重启音频服务后生效"));
    refreshApoState();
}

void AppController::runSystemAction(const QString &action)
{
    if (action == QLatin1String("install"))
        installApo();
    else if (action == QLatin1String("attach"))
        setEndpointAttached(apoTargetDevice(), true);
    else if (action == QLatin1String("restart"))
        restartAudio();
    else if (action == QLatin1String("master"))
        setMasterEnabled(true);
}

} // namespace dreamdsp
