# PeaceQT 自研 DSP 效果:技术决策书

> **命名与路径校正(先说清楚)**:本次核查中 `I:\Qt\PeaceQT` 不存在。实际项目是 `I:\Qt\DreamDSP`(DreamDSP 0.1.0,Qt 6.7 + QML + HuskarUI,单 exe target,`src/{core,app,platform,ui}`)。下文一律指这个项目;凡出现 "PeaceQT" 请读作 DreamDSP。所有 "现有文件" 的引用都已在磁盘上确认存在:`src/core/ApoConfig.cpp`、`src/core/Biquad.cpp`、`src/platform/ApoLocator.cpp`、`src/platform/AudioDevices.cpp`、`src/platform/LoopbackCapture.cpp`、`src/platform/PeakMeter.cpp`。

---

## 1. 结论先行

**推荐路线 (b):自己写 APO DLL**,但**必须先把 DSP 做成一个不依赖 Windows 的离线静态库并用文件级 harness 验证通过**,再谈进 audiodg;并且**保留现有 ApoConfig 生成器,让 EQ/线性滤波继续走 Equalizer APO**,我们的 APO 只承担 APO 配置语言表达不了的非线性/有状态效果。

**一句话理由**:ViPER4Windows 在 2026 年就是一个普通用户态 COM APO DLL(本机 `C:\Program Files\ViPER4Windows` 里没有任何 `.sys`/`.inf`),证明这条路一个人能走完;而路线 (a) 被一个已确认的事实直接判死——Equalizer APO 每次 config 变更都**析构并重建整条 filter chain**(`FilterEngine.cpp` 里 `previousConfig->~FilterConfiguration(); MemoryHelper::free(...)`),所以拖一下混响 decay 滑块就会把混响尾巴和压缩器包络一起清空;路线 (c) 要内核驱动签名(EV 证书 + Partner Center)外加 15–45 ms 额外延迟。

**最关键的风险**(一句话):我们的 DSP 在 `audiodg.exe` 里崩一次,**全机所有厂商的 APO 一起哑**(本机同时装着 Equalizer APO、ViPER4Windows、Bongiovi),连续 10 次失败后 Windows 会把该端点的 `PKEY_Endpoint_Disable_SysFx` 置 1、**静默关掉这个端点上全部系统效果**,而此时用户没有可用的声音听我们解释发生了什么。

**两个必须先跑的实验(在写任何安装器代码之前)**:
1. **槽位实验**:在本机默认端点写 modern MFX(`{d04e05a6-...},6`)会不会让 Equalizer APO 的 legacy LFX/GFX(`,1`/`,2`)失效?`CompositeFX`(`,13`/`,14`/`,15`,REG_MULTI_SZ 列表)能不能让我们和 Equalizer APO 共存?这决定了要不要写 child-APO 链式加载那一整套机制。
2. **CAPX 实验**:一个手工写 FxProperties 注册、**没有 INF/驱动包**的 APO,`IAudioProcessingObjectNotifications::HandleNotification` 到底会不会被调用?微软文档明确写着 Default 子存储 "is populated from the INF file",我们没有 INF。这决定了实时参数通道是走官方支持路径还是自建管道。

顺手在同一个探针里量到:`APOProcess` 实际的 `frameCount`(以及是否每次都一样)、以及把 `DisableProtectedAudioDG` 设回 0 之后我们的 DLL 还能不能加载。

---

## 2. 三条路线对比

| 维度 | (a) 自写 VST 交给 APO 托管 | **(b) 自写 APO DLL(推荐)** | (c) 虚拟声卡 / 其它 |
|---|---|---|---|
| **能力上限** | 低。只能 VST2(APO 只 `GetProcAddress("VSTPluginMain")`,无 VST3/CLAP/LV2,且上游 ticket #275 至今 Open)。多声道被拆成 N 个独立实例(`effectCount = (channelCount + effectChannelCount - 1) / effectChannelCount`),立体声联动/跨声道处理直接报废 | 高。deinterleaved float32、完整声道语义自己定义、任意非线性/有状态算法。唯一硬限制:一进一出、不能改设备设置、改声道数(大概率)只有 SFX 能做 | 最高(自己进程里想干啥干啥,能开调试器),但**代价是用户要手动切默认播放设备** |
| **开发工作量** | DSP 本身 + VST2 AEffect 壳(~400 行)。看起来最省,实际是死路 | DSP + COM 壳(~1200–1800 行)+ 注册/看门狗/恢复(~1500–2500 行)。**非 DSP 部分占 80%** | DSP + WDM/PortCls 驱动 + 用户态引擎 + 设备路由 UI。参考 FxSound(GPL3):驱动一个 repo、app 一个 repo |
| **安装摩擦** | 低(拷 DLL 到 `<InstallPath>\VSTPlugins`——**本机该目录不存在**,相对路径一律 "File %s not found"),但依赖用户已装 Equalizer APO | 中。需提权写 HKLM CLSID + 端点 FxProperties,并 `Restart-Service audiosrv`(**这一下全机所有 app 瞬断音**)。Windows 功能更新换端点 GUID 后要重新注册 | 高。重启、驱动安装警告、用户手动改默认设备、每个 app 的独占模式都要单独教育 |
| **签名要求** | 无(宿主已加载) | **形式上无,实际上是定时炸弹**。本机能加载未签名 APO 只因为 `DisableProtectedAudioDG=1`——而这个键是 **Equalizer APO 的安装器写的**。微软文档只正面说了 "graph is running protected content, and the APO is not properly signed" 会导致 `CoCreateInstance` 失败,**没说反面**;"未签名 APO 在干净机器上能加载" 属于**未验证**。另外必须 `/MANIFEST:NO`,否则测试键一撤就永久加载失败(即使 WHQL 签名) | **内核签名,最贵**。2026 年 4 月起微软撤销 cross-signed 驱动信任(先 evaluation mode,阈值已从 100h 改到 250h),只剩 WHCP/attestation + 账号 EV 证书;且 2026-02-15 起代码签名证书寿命上限 1 年。**注意:这条只管内核驱动,不影响用户态 APO DLL** |
| **失败模式(会不会把声音搞没)** | **会,但只搞没我们自己的**。APO 用 SEH 包住 VST 调用,崩了就 `memcpy` 直通并记日志——**静默 bypass**,用户听到的是没处理过的原声,只能去读日志。ERA Noise Reducer 那种 "所有声音都没了" 的案例也真实存在 | **会,而且是最坏的一种**。崩在 `APOProcess` 里 → audiodg 挂 → **全机所有 app 无声** → 用户没声音可以听我们报错 → 10 次后 Windows 把整个端点的 SysFx 关掉(**连 Equalizer APO 和 ViPER 一起关**),控制面板里 "增强" 变灰、无任何解释 | **不会搞没系统声音**(我们的进程崩了只影响经过虚拟设备的流),这是它唯一的结构性优势 |
| **实时参数控制** | **做不到**。唯一通道是 config.txt,而每次写文件都触发全链析构重建 —— reverb 尾巴断、压缩器包络清零、VST 实例重新构造。Peace 作者本人证实 "constantly a sequence of removing the former plugin and creating a new one"。只对无状态线性滤波(EQ/gain/delay)可用 | **能做好**。GUI ↔ audiodg 之间开命名管道(Equalizer APO 的 `DeviceSelector.exe` 已经证明这条边界可穿:它 `CreateNamedPipeW`,APO 端 `"Could not connect to named pipe: %s"`),非 RT 线程收参数 → POD 结构 → `std::atomic` 指针交换 → `APOProcess` 只做 exchange + 拷贝 + 一极平滑 | **最简单**。同进程内普通原子变量,零约束 |

**关于 (a) 的额外否决理由**(任一条单独就够):

- Equalizer APO 不用 Steinberg SDK,它 vendor 了 LMMS 的 GPLv2 clean-room `aeffectx.h`(VeSTige)。Steinberg 已对携带该头文件的 repo 发过 DMCA。**注意:这条我这次没有亲自 fetch `helpers/aeffectx.h` 验证,brief 的 VeSTige 出处属于未独立确认**——但既然 (a) 因为技术原因已经出局,这个法律问题不需要我们解决。
- `VSTPlugin:` 命令**不在官方 Configuration Reference wiki 里**,是 Editor 生成的私有命令,可以在任意版本无预警改语法。
- 参数按名字寻址、按索引下发、`wcstof` 解析失败静默变 0.0、奇数 token 静默丢最后一个、引号不配对不报错。任何经 config.txt 往返的参数都会**静默损坏成 0.0 而不是报错**。
- ChunkData 是 Base64 不透明 blob,用户不可手编,我们也没法在上面做 GUI——**这直接杀死 "PeaceQT 给第三方 VST 套 ViPER 风格旋钮" 这个想法**。

**关于 (c) 的补充**:本机已装 Voicemeeter,它的 Potato 版带 ASIO Insert 驱动,专门用来把外部 VST 宿主插进信号链。如果要给高级用户一个零驱动逃生口,**文档化 "DreamDSP 作为 Voicemeeter Potato ASIO Insert" 比自己写虚拟声卡便宜一个数量级** —— 成本只是一个 ASIO client。这是 nice-to-have,不是主线。

---

## 3. 推荐路线的详细设计

### 3.1 核心决策:三层切分,DSP 层不认识 Windows

最重要的一条工程纪律:**DSP 代码的第一次执行必须发生在离线 harness 里,而不是用户机器的 audiodg 里**。为此把 DSP 抽成一个纯 C++17 静态库,不 include 任何 `<windows.h>`,不分配、不加锁、不做 I/O。

```
I:\Qt\DreamDSP\
├── CMakeLists.txt                    # 顶层:add_subdirectory 三个 target
├── src\                              # ── 现有 Qt GUI(基本不动)
│   ├── core\
│   │   ├── ApoConfig.{h,cpp}         # 保留!EQ/Preamp/Convolution 继续走这里
│   │   ├── Biquad.{h,cpp}            # 保留,dspcore 复用(挪到 dsp/ 或双向 include)
│   │   ├── PeacePreset.{h,cpp}       # 保留
│   │   └── DspParams.h               # 新增:GUI 侧的参数模型(QObject/Q_PROPERTY)
│   ├── platform\
│   │   ├── ApoLocator.{h,cpp}        # 扩展:增加"我们自己的 APO 注册状态"检测
│   │   ├── AudioDevices.{h,cpp}      # 扩展:端点枚举 + FxProperties 读写
│   │   ├── DspBridge.{h,cpp}         # 新增:命名管道 server,推参数给 APO
│   │   ├── ApoInstaller.{h,cpp}      # 新增:提权注册/注销(调用 helper)
│   │   ├── ApoWatchdog.{h,cpp}       # 新增:定时校验注册是否还在
│   │   └── LoopbackCapture / PeakMeter  # 不动:只做表头/频谱,永远不进音频路径
│   └── app\, ui\, qml\               # 不动
│
├── dsp\                              # ── 新增 target: dreamdsp_core (STATIC)
│   ├── CMakeLists.txt                #    纯 C++17,零 Windows 依赖,可跨平台单测
│   ├── DspTypes.h                    #    Params POD、常量、SampleRate
│   ├── DspChain.{h,cpp}              #    效果链编排 + 参数平滑
│   ├── Oversampler2x.{h,cpp}         #    共享 polyphase halfband
│   ├── effects\
│   │   ├── VirtualBass.{h,cpp}       #    MaxxBass 式 NLD
│   │   ├── Clarity.{h,cpp}           #    sharpening + exciter
│   │   ├── TubeStage.{h,cpp}         #    tanh 偏置 + ADAA-1 + DC blocker
│   │   ├── Compressor.{h,cpp}        #    Giannoulis log-domain feedforward
│   │   ├── MultibandComp.{h,cpp}     #    LR4 树 + N 个 Compressor
│   │   ├── PlateReverb.{h,cpp}       #    Dattorro
│   │   ├── Freeverb.{h,cpp}          #    public domain,可直抄
│   │   ├── Crossfeed.{h,cpp}         #    "Cure+" 的我方实现(见 §4)
│   │   └── RoomSim.{h,cpp}           #    环境模拟 FDN(v2)
│   └── tests\
│       └── (Catch2 或裸 main)
│
├── apo\                              # ── 新增 target: DreamDspApo (SHARED)
│   ├── CMakeLists.txt                #    /MANIFEST:NO  /MT  无 Qt  无 CRT 动态依赖
│   ├── DllMain.cpp                   #    DllGetClassObject / DllCanUnloadNow
│   ├── ClassFactory.{h,cpp}          #    自写,不抄 EqualizerAPO(GPLv2)
│   ├── DreamApo.{h,cpp}              #    四个接口 + RT 纪律
│   ├── ParamChannel.{h,cpp}          #    管道 client + 无锁 handoff
│   ├── RtAlloc.{h,cpp}               #    AERT_Allocate / VirtualLock 包装
│   ├── ApoLog.{h,cpp}                #    只在 LockForProcess/非 RT 线程写
│   └── DreamDspApo.def
│
├── harness\                          # ── 新增 target: dspharness (CONSOLE EXE)
│   └── main.cpp                      #    wav in → dreamdsp_core → wav out
│
├── installer\
│   ├── register.reg / unregister.reg #    离线恢复用,随包发布
│   └── SAFEMODE.txt                  #    没声音时的救命文档
└── third_party\, docs\, scripts\
```

### 3.2 关键接口

**(1) DSP 层的唯一契约** —— 和 APO 的 `IAudioProcessingObjectRT` 对齐,但不引用它:

```cpp
// dsp/DspTypes.h
#pragma once
#include <cstdint>

namespace dream {

// 纯 POD。不含指针、std::string、可变长容器。
// 这个结构体同时是:GUI 的参数模型、管道的线协议、RT 线程读的目标。
#pragma pack(push, 1)
struct Params {
    uint32_t version;          // 结构体版本,不匹配一律丢弃
    uint32_t sequence;         // GUI 递增;APO 侧只用于去重
    uint32_t masterEnabled;    // 0 = 全链旁路(kill switch,见 §7)

    // --- 虚拟低音 ---
    uint32_t bassEnabled;  float bassCutoffHz;   // 40..320
    float    bassAmount;   float bassKeepFund;   // 0..1
    // --- 清晰度 / 激励器 ---
    uint32_t clarityEnabled; uint32_t clarityMode; // 0=sharpen 1=exciter 2=both
    float    clarityAmount;  float exciterFreqHz;  // 700..8000
    // --- 胆机 ---
    uint32_t tubeEnabled;  float tubeDrive;  float tubeBias;   float tubeMix;
    // --- 压缩器 ---
    uint32_t compEnabled;  float compThreshDb; float compRatio; float compKneeDb;
    float    compAttackMs; float compReleaseMs; float compMakeupDb;
    uint32_t compAutoMakeup;
    // --- 混响 ---
    uint32_t revEnabled;   float revPreDelayMs; float revRoomSize; float revDamping;
    float    revBandwidth; float revDensity;    float revDecay;    float revWet;
    // --- Crossfeed ---
    uint32_t xfeedEnabled; float xfeedAmount;   float xfeedFreqHz;

    uint32_t sequenceEnd;      // 必须等于 sequence,否则视为撕裂
};
#pragma pack(pop)

static constexpr uint32_t kParamsVersion = 1;

} // namespace dream
```

```cpp
// dsp/DspChain.h
#pragma once
#include "DspTypes.h"

namespace dream {

class DspChain {
public:
    // 分配阶段。允许 new/malloc。由 APO 的 LockForProcess 或 harness 调用。
    // maxFrames 必须按宿主给的 maxFrameCount 传,内部所有临时缓冲按它开。
    // 返回 false 表示不支持该格式(声道数 > kMaxChannels 等)。
    bool prepare(float sampleRate, unsigned maxFrames, unsigned channels);

    // 释放阶段。UnlockForProcess 调用。
    void release();

    // 非 RT 线程调用:把新参数塞进待生效槽。可以从任意线程调,不阻塞。
    void publish(const Params& p);

    // ===== 以下只允许从 RT 线程调用 =====
    // 严禁:分配、加锁、日志、文件、COM、异常、函数内 static。
    // deinterleaved,每声道一个指针,frames <= prepare 时的 maxFrames。
    // in 与 out 可以相同(原地),也可以不同。
    void process(float* const* out, const float* const* in,
                 unsigned channels, unsigned frames) noexcept;

    // 我们向宿主申报的额外延迟(样本数)。目前恒为 0——所有已选算法零延迟。
    unsigned latencySamples() const noexcept { return 0; }

    static constexpr unsigned kMaxChannels = 8;
private:
    // 三槽无锁交接,见 §5
};

} // namespace dream
```

**(2) APO 层只有一层薄壳**,`APOProcess` 里除了转 deinterleave 和调 `DspChain::process` 什么都不做:

```cpp
// apo/DreamApo.cpp (节选,真实可编译的骨架)
#pragma AVRT_CODE_BEGIN
STDMETHODIMP_(void) DreamApo::APOProcess(
    UINT32 inConn,  APO_CONNECTION_PROPERTY** ppIn,
    UINT32 outConn, APO_CONNECTION_PROPERTY** ppOut)
{
    UNREFERENCED_PARAMETER(inConn);
    UNREFERENCED_PARAMETER(outConn);

    const UINT32 frames = ppIn[0]->u32ValidFrameCount;
    float* dst = reinterpret_cast<float*>(ppOut[0]->pBuffer);
    const float* src = reinterpret_cast<const float*>(ppIn[0]->pBuffer);

    switch (ppIn[0]->u32BufferFlags)
    {
    case BUFFER_VALID:
        // 交错 -> 平面。m_planarIn/m_planarOut 在 LockForProcess 里
        // 用 AERT_Allocate 分配并 VirtualLock,大小 = maxFrameCount * channels。
        deinterleave(m_planarIn, src, m_channels, frames);
        m_chain.process(m_planarOut, m_planarIn, m_channels, frames);
        interleave(dst, m_planarOut, m_channels, frames);
        break;

    case BUFFER_SILENT:
        // 关键:静音时仍要跑,否则混响尾巴会被切断。
        // 但要有一个"连续 N 秒静音后停跑"的计数器省 CPU。
        if (m_silentFrames < m_tailFrames) {
            zero(m_planarIn, m_channels, frames);
            m_chain.process(m_planarOut, m_planarIn, m_channels, frames);
            interleave(dst, m_planarOut, m_channels, frames);
            m_silentFrames += frames;
            ppOut[0]->u32BufferFlags = BUFFER_VALID;
            ppOut[0]->u32ValidFrameCount = frames;
            return;
        }
        // 尾巴放完了,原样传静音标志
        ppOut[0]->u32BufferFlags = BUFFER_SILENT;
        ppOut[0]->u32ValidFrameCount = frames;
        return;

    default:
        // 未知标志:原样直通,绝不猜。
        if (dst != src) memcpy(dst, src, size_t(frames) * m_channels * sizeof(float));
        break;
    }

    m_silentFrames = 0;
    ppOut[0]->u32ValidFrameCount = frames;
    ppOut[0]->u32BufferFlags = BUFFER_VALID;
    // 注意:绝不能修改 ppOut 数组本身,只能设它指向的 property。
}
#pragma AVRT_CODE_END
```

四个接口按微软要求实现:`IAudioProcessingObject`(`Initialize` / `IsInputFormatSupported` / `IsOutputFormatSupported` / `GetLatency`)、`IAudioProcessingObjectConfiguration`(`LockForProcess` / `UnlockForProcess`,**所有分配都在这里**)、`IAudioProcessingObjectRT`(`APOProcess` / `CalcInputFrames` / `CalcOutputFrames`)、`IAudioSystemEffects`(标记接口)。**绝不暴露 `IAudioProcessingObjectVBR`**(微软明文禁止)。

是否用 `CBaseAudioProcessingObject`:**用**。微软确认接受 float32 的话只需实现三个方法(`IsInputFormatSupported`、`APOProcess`、`ValidateAndCacheConnectionInfo`)。它拖进 WDK 依赖,但省下的样板代码远大于这个成本,而且 Equalizer APO 手写 COM 的那套代码是 **GPLv2,不能抄**。

### 3.3 和现有 PeaceQT / ApoConfig 的关系

**ApoConfig 生成器留,而且是 EQ 的唯一实现路径。** 理由不是"过渡期兼容",而是分工正确:

| 效果类型 | 归属 | 理由 |
|---|---|---|
| 参数 EQ / 图形 EQ / Preamp / Delay / Copy / 声道路由 | **Equalizer APO config**(现有 `ApoConfig.cpp`) | 无状态、线性、无损、已经能用、改 config 触发全链重建对它无害(没有状态可丢) |
| 清晰度的 "醇氧+ / OZone" 模式 | **Equalizer APO config**(一行高架滤波器) | 已确认 ViPER 的 OZONE 就是一个 8250 Hz 高架 biquad,`SetGain(gain + 1.0)`。**写一行 `Filter: ON HS Fc 8250 Hz Gain X dB` 比自己算便宜到不值得讨论** |
| 卷积混响 / IR | **Equalizer APO config**(`Convolution:` 命令) | 零 DSP 工作量。代价:换 IR 会触发全链重建,所以它不是可实时调的旋钮,UI 上要明确区分 |
| 虚拟低音 / 激励器 / 胆机 / 压缩器 / 算法混响 / crossfeed | **我们自己的 APO** | 非线性或有状态,config.txt 完全表达不了 |

这条分工带来一个很大的额外好处:**我们不需要为了塞进去而把 Equalizer APO 挤掉**。它继续占它的 legacy LFX/GFX 槽,我们找一个不冲突的位置(见 §7 的槽位风险)。同时也意味着 DreamDSP 依然**硬依赖 Equalizer APO 已安装**——这是既有事实(现在 100% 依赖),不是新增负担。

GUI 侧的改动量很小:`AppController` 多一个 `DspParams` QObject,滑块 `valueChanged` → `DspBridge::publish()`(节流到 ~30 Hz)→ 管道;EQ 部分照旧走 `ApoConfig` 写文件。两条通道互不干扰。

---

## 4. DSP 实现清单

代码量是**净新增可编译 C++ 行数**(不含空行注释),按"我自己写"估。

| 效果 | 算法 | 关键参数 | 需要 oversampling? | 引入延迟? | 代码量 | 备注 |
|---|---|---|---|---|---|---|
| **ViPER 低音** | LR4 分频 @fc + NLD 缺失基频合成(MaxxBass US5930373A,**2017-04-04 到期**,可实现);全波整流(出 2/4 次谐波)+ 立方项(出 3/5 次)混合;30 ms 单极包络响度匹配;谐波支路带通 [fc, ~5fc] | `fc` 40–320 Hz(=「音箱尺寸」旋钮的真实含义)、`amount` 0–1、Residue Expansion Ratio 1.34(2 次主导)/1.74(3 次主导)、是否删除基频 | **不需要**。整流器谐波幅度按 1/(n²−1) 衰减,fc≤320 Hz 时折返分量在 −80 dB 以下 | 无 | ~200 | 另做一个 "ViPER 兼容" 子模式:60 Hz Q=0.53 双 biquad + `sat_mix`(knee 0.5,`0.5+(d−0.5)/sqrt(1+(d−0.5)²)`)+ 30 ms 平滑,用于让移植过来的 ViPER 预设声音一致 |
| **清晰度** | 三模式。① Natural = 边沿锐化 `y = x + g(x − x[n−1])`;② 醇氧+ = **不做 DSP**,发一行高架滤波器给 Equalizer APO;③ X-HiFi / 激励器 = Aphex 式**并联旁链**:HP(fc) → 非线性 → 可选顶端 LP → 增益 → 与干路相加,干路保持比特精确 | `mode`、`amount` 0–1、激励器 `fc` 700–8000 Hz | 激励器支路 **需要 2x**(HP 后的信号带宽很宽,谐波必然折返) | 无(2x polyphase halfband 群延迟几个样本,可以不申报;若要严格对齐,干路补同样延迟) | ~120 + 40 | ViPER 的 `noise_sharpening_` 和 `hifi_` 内部我这次**没读**,所以我们的实现是"功能对等"而非"比特对等",UI 文案不要暗示等价 |
| **Cure+** | ⚠️ **不知道它是什么。** ViPERDSP 里有 `Cure.cpp`,本次没人读过;网上关于它是"耳机串扰消除"的说法**没有可引用来源**。**不要当事实。** 我方做法:实现一个我们自己定义的 crossfeed(Bauer/Meier 型:对侧信号经 ~700 Hz 一阶低通 + 300–500 µs 延迟 + −3…−6 dB 后交叉相加),命名为「耳机串音」,**不叫 Cure+**,不宣称兼容 | `amount` 0–1、`fc` 500–1200 Hz | 不需要(纯线性) | 有,~0.3–0.5 ms(交叉延迟),但只在开启时。可以不申报,也可以老实报 | ~80 | 如果一定要复刻 ViPER 的 Cure,先派人读 `ViPERDSP/viper/effects/Cure.cpp`——这是一个小时的工作,别猜 |
| **胆机** | `f(x) = tanh(k·x + b) − tanh(b)`(偏置产生偶次谐波);ADAA 一阶(`F(x) = log(cosh(kx+b))/k − tanh(b)·x`,`|Δx|<1e-5` 时回退到中点求值);后接 DC blocker `y = x − x[n−1] + R·y[n−1]` | `drive` k 1–20、`bias` b 0–0.6、`mix` | **需要,2x**。DAFx-16 明确结论是 ADAA + 低阶 oversampling 组合远优于纯高倍 oversampling | ADAA-1 有 0.5 样本群延迟,**不需要申报**;干湿混合若在意可以给干路补半样本 | ~120 | **ViPER 的胆机是假的**——`TubeSimulator.cpp` 全文只有 `acc = (acc + x) / 2`,零非线性、零 oversampling、零 DC blocker。这是我们能诚实说"做得比 ViPER 好"的地方 |
| **压缩器** | Giannoulis/Massberg/Reiss 前馈对数域。静态曲线三段式(膝下/膝内二次/膝上);检波器**放在增益计算器之后**,对数域分支平滑,`α = exp(−ln9/(fs·T))` | `T` −60…0 dB、`R` 1–20(+∞=限制器)、`W` 0–24 dB(默认 6)、`attack` 0.1–100 ms、`release` 20–2000 ms、`makeup`、auto-makeup `M = −x_sc(0)` | **不需要**。非线性只在被 attack/release 单极重度平滑过的控制路径里 | 无(无 lookahead)。若以后加 lookahead 限制器,必须在 `GetLatency` 里如实申报 | ~150 | ⚠️ **符号约定陷阱**:MathWorks 的 `g_c` 是 dB 增益(≤0),attack 分支是 `g_c ≤ g_s`;brief 代码里的 `cDb` 是正的衰减量,分支是 `cDb > yL`。两种各自自洽,**混用会把 attack 和 release 反过来**。选一种写进注释并单测 |
| 多段压缩(可选) | LR4 分频树 + N 个上面的 Compressor | 分频点数组 | 不需要 | 无(LR4 是 IIR) | +80 | v2 |
| **混响** | **Dattorro plate**(JAES 45(9), 1997)。预延迟 → bandwidth 单极 LP → 4 个输入扩散全通(142/107/379/277)→ 8 字形 tank(A: 672+mod/4453/1800/3720;B: 908+mod/4217/2656/3163),全部 ×fs/29761 缩放并四舍五入;14 个输出抽头权重 0.6、正负号必须保留 | 预延迟 0–200 ms、room size(tank 长度全局缩放 0.5–2×)、damping(tank 单极)、bandwidth(输入单极)、density(输入扩散系数 0.750/0.625)、decay(0–0.9999)、early/late 混合、wet | 不需要(全线性) | 有,= 预延迟长度。**如实在 `GetLatency` 申报**,否则视频会失去唇同步 | ~350 | 三个单极 LP **必须直接 I 型**(Dattorro 明确要求,否则内部节点提前削波)。调制 LFO ~1 Hz、峰值 8–16 样本、**逐样本更新**,按块更新会往音频里注入 aliasing。room size 改变要重开延迟线 → **不能平滑,只能双实例交叉淡化** |
| Freeverb(备选/兼容) | 8 并联 LBCF + 4 串联全通,`tuning.h` 常数,stereospread 23 | roomsize→feedback 0.70–0.98、damp→0–0.4、wet/dry/width | 不需要 | 无 | ~250,**设计工作量为 0** | `tuning.h` 明确标 **PUBLIC DOMAIN**(Jezar @ Dreampoint, 2000)——**这是唯一可以逐字抄进闭源产品的参考代码**。ViPER 的混响就是 Freeverb 包装(`SetRoomSize/SetWidth/SetDamp/SetWet/SetDry` + `ProcessReplace`)。但 Freeverb **没有** 预延迟/bandwidth/density/早反射,截图里那些旋钮它给不了 |
| **环境模拟** | ⚠️ ViPER 的 `VHE_L0..L4` 内部**未读**,不知道具体算法。**v1 不自研**:直接用 Equalizer APO 的 `Convolution:` 命令 + 我们随包发的房间 IR,零 DSP 代码。v2 如果需要可实时调,再写 8×8 Hadamard FDN + 早反射抽头延迟线 | v1: IR 文件选择;v2: 房间尺寸/材质/早反射密度 | v1 不适用;v2 不需要 | v1: APO 卷积自身延迟**未测**;v2: 早反射预延迟 | v1: **0**;v2: ~250 | 卷积和算法混响**不要在 UI 里互相替代**:卷积给的是精确不可调的房间,算法给的是连续可调零延迟。分成两个功能 |
| 共享基础设施 | 2x polyphase halfband oversampler、LR4 分频、参数平滑(一极 + 启用时 1/fs 防爆音斜坡)、denormal 防护 | — | — | — | ~350 | denormal:`_mm_setcsr(_mm_getcsr() \| 0x8040)` 设 FTZ+DAZ,**在 process 入口设、出口恢复**(我们是 audiodg 的客人,同线程可能还有别家 APO);外加每条反馈路径注入 1e-20 直流 |

**dspcore 净新增合计:约 2000–2500 行。** 这是整个项目里**最容易、最可控、风险最低**的部分。

---

## 5. 实时参数控制

### 5.1 已被否决的方案

- **config.txt**:全链析构重建,状态全丢。已确认(`FilterEngine.cpp` 的 `~FilterConfiguration()` + `MemoryHelper::free()`)。**只能用于 EQ 这类无状态线性滤波。**
  - 补充一个反直觉的正面事实:config.txt 通道**不会爆音、不会断音**——重建在后台通知线程做,RT 线程用 `sampleRate/100`(10 ms)等长交叉淡化切过去。所以现有 DreamDSP 的 EQ 滑块体验其实是好的,不要因为要上 APO 就把它改掉。
  - 但交叉淡化期间**两条链都完整跑**(`nextConfig->read(); nextConfig->process();` 无条件执行),瞬时 2× CPU。这也是为什么我们**不应该**在自己的 APO 里用整链交叉淡化来处理旋钮变化——只对"增删效果"这种结构性变更用它。
- **GUI 创建 `Global\` 共享内存**:非提权进程做不到。`SeCreateGlobalPrivilege` 是必需的,本机用户令牌里没有(`whoami /priv` 只有 SeShutdown/SeChangeNotify/SeUndock/SeIncreaseWorkingSet/SeTimeZone),且不是管理员。
  - ⚠️ 但 brief 把这条**过度概括**了:微软的限制**只针对 file-mapping 对象和符号链接对象**,不包括 event/mutex/semaphore/waitable timer。所以**非提权 GUI 可以创建 `Global\DreamDSP_ParamsChanged` 这个事件**,只是不能创建那段共享内存。

### 5.2 推荐方案:命名管道 + 无锁 handoff

选管道不选共享内存,三个理由:(1) `\\.\pipe\` 不受会话命名空间约束、不需要任何特权;(2) Equalizer APO 的 `DeviceSelector.exe` 已经在这台机器上证明这条边界可穿(它 `CreateNamedPipeW`,`EqualizerAPO.dll` 里有 `"Could not connect to named pipe: %s"`);(3) **`MapViewOfFile` 映射的视图是可分页内存,从 `APOProcess` 直接读违反微软 "All buffers that are processed by the APO must be nonpageable / All code and data in the process path must be nonpageable"**,要合规就得 `VirtualLock`,平白多一层。

拓扑:**GUI = 管道 server(它拥有 DACL),APO = client**。

```cpp
// src/platform/DspBridge.cpp —— GUI 侧,创建管道并授权给 audiosrv
// 关键:默认 DACL 下 LocalService 打不开。必须显式授权服务 SID。
static bool makePipeSecurity(SECURITY_ATTRIBUTES& sa, std::vector<BYTE>& sdBuf)
{
    // D:  (A;;GA;;;BA)      Administrators 全权
    //     (A;;GA;;;SY)      SYSTEM 全权
    //     (A;;GRGW;;;S-1-5-80-...)  NT SERVICE\Audiosrv 读写
    // 用 LookupAccountNameW(L"NT SERVICE\\Audiosrv") 取 SID 转字符串拼进 SDDL,
    // 不要硬编码 —— 服务 SID 是名字的 SHA-1,可以算但没必要。
    PSID sid = nullptr; DWORD cbSid = 0, cbDom = 0; SID_NAME_USE use;
    LookupAccountNameW(nullptr, L"NT SERVICE\\Audiosrv", nullptr, &cbSid,
                       nullptr, &cbDom, &use);
    std::vector<BYTE> sidBuf(cbSid); std::vector<wchar_t> dom(cbDom);
    sid = reinterpret_cast<PSID>(sidBuf.data());
    if (!LookupAccountNameW(nullptr, L"NT SERVICE\\Audiosrv", sid, &cbSid,
                            dom.data(), &cbDom, &use))
        return false;

    LPWSTR sidStr = nullptr;
    if (!ConvertSidToStringSidW(sid, &sidStr)) return false;
    std::wstring sddl = L"D:(A;;GA;;;BA)(A;;GA;;;SY)(A;;GRGW;;;";
    sddl += sidStr; sddl += L")";
    LocalFree(sidStr);

    PSECURITY_DESCRIPTOR sd = nullptr; ULONG sdLen = 0;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &sd, &sdLen))
        return false;
    sdBuf.assign(static_cast<BYTE*>(sd), static_cast<BYTE*>(sd) + sdLen);
    LocalFree(sd);

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sdBuf.data();
    sa.bInheritHandle = FALSE;
    return true;
}
// 管道名带端点 GUID,支持多端点各一条:
//   \\.\pipe\DreamDSP.Params.{endpoint-guid}
// 消息模式(PIPE_TYPE_MESSAGE),每条消息就是一个 dream::Params。
// GUI 侧节流:QTimer 30 Hz 合并,滑块拖动期间不要每个 mouseMove 都发。
```

APO 侧:**一个专门的非 RT 线程**读管道,`APOProcess` 只做指针交换。

```cpp
// apo/ParamChannel.h —— 三槽无锁交接。RT 线程永不分配、永不释放、永不阻塞。
class ParamChannel {
public:
    // LockForProcess 里调用:分配 3 个槽(AERT_Allocate + VirtualLock),
    // 起管道读线程。UnlockForProcess 里停线程、释放槽。
    bool start(const wchar_t* pipeName);
    void stop();   // 必须先停线程再释放槽,否则 use-after-free

    // 管道线程调用
    void publish(const dream::Params& p) noexcept {
        Slot* s = m_free.exchange(nullptr, std::memory_order_acquire);
        if (!s) return;                       // RT 还没还槽:合并,丢弃本次(下一帧会补)
        s->p = p;
        Slot* old = m_pending.exchange(s, std::memory_order_release);
        if (old) m_free.store(old, std::memory_order_release);  // 被更新的取代
    }

    // RT 线程调用。无分配、无锁、无系统调用。
    const dream::Params* take() noexcept {
        Slot* s = m_pending.exchange(nullptr, std::memory_order_acquire);
        if (!s) return nullptr;
        if (m_inUse) m_free.store(m_inUse, std::memory_order_release);
        m_inUse = s;
        return &s->p;
    }
private:
    struct Slot { dream::Params p; };
    std::atomic<Slot*> m_pending{nullptr};
    std::atomic<Slot*> m_free{nullptr};
    Slot* m_inUse = nullptr;
};
```

`DspChain::process` 里:

```cpp
if (const Params* p = m_channel.take()) {
    if (p->version == kParamsVersion && p->sequence == p->sequenceEnd)
        m_target = *p;                       // 纯 POD 拷贝
}
// 连续参数:每块一极平滑,杀掉 zipper noise
smooth(m_cur.compThreshDb, m_target.compThreshDb, 0.05f);
smooth(m_cur.revWet,       m_target.revWet,       0.05f);
smooth(m_cur.tubeDrive,    m_target.tubeDrive,    0.05f);
// 结构性参数(revRoomSize / revPreDelayMs 会改延迟线长度):不平滑。
// 走双实例交叉淡化,或只在块边界应用。
```

**为什么不抄 ViPER 的做法**:ViPER4Windows 的 APO 用 `FindFirstChangeNotificationW` 监视目录、重读 2700 字节的 `EffectConfig.bin`,并且导入 `EnterCriticalSection`/`LeaveCriticalSection`——也就是在音频路径上或附近加锁,这**违反微软的 RT 规则**。它能用不代表它对。唯一值得抄的是**文件放 `C:\Program Files\ViPER4Windows\DriverComm` 而不是用户目录**——因为 audiodg 读不到 `C:\Users`。我们的落盘配置一律放 `%ProgramData%\DreamDSP` 或安装目录。

### 5.3 官方路径(待验证):Windows 11 CAPX

微软确实有一条支持的通道:GUI 通过 `IAudioSystemEffectsPropertyStore` 拿到属性存储(Volatile 子存储放实时旋钮、User 子存储放预设,后者"persisted by the OS across upgrades and migrations"),APO 实现 `IAudioSystemEffects3` + `IAudioProcessingObjectNotifications`,收 `APO_NOTIFICATION_TYPE_AUDIO_SYSTEM_EFFECTS_PROPERTY_CHANGE`。文档白纸黑字写着"readable and writable by ... non-admin desktop applications",HLK 要求只约束 **随设备出厂**的 APO,不约束我们。

**但**:Default 子存储"is populated from the INF file",我们没有 INF。**手工 FxProperties 注册的 APO 能不能收到 `HandleNotification`,完全未测。** 这是 §1 的探针 2。

- 如果能用 → 采用它做主通道,管道降级为 Windows 10 回退路径(用 `APOInitSystemEffects3` vs `APOInitSystemEffects2` 探测)。
- 如果不能用 → 管道就是唯一方案,没有损失。

无论走哪条,回调线程的纪律一样:**只读属性 → 转成 POD → `publish()`。零分配、零构造滤波器。**而且必须显式同步 `UnlockForProcess` 和在途通知,否则设备切换时会 use-after-free(微软原文点名了这一点)。

### 5.4 audiodg.exe 的安全边界(硬约束清单)

1. **LocalService 令牌,读不到 `C:\Users\<user>`**。→ 所有配置、IR、预设放 `%ProgramData%\DreamDSP` 或安装目录。日志落到 `C:\Windows\ServiceProfiles\LocalService\AppData\Local\Temp`。
2. **不能显示任何 GUI**。→ 所有用户交互在 Qt 进程里,APO 只做数据面。
3. **可能是 protected process**。本机 `DisableProtectedAudioDG=1`(**Equalizer APO 的安装器设的,不是我们**),所以本机观察到的一切 IPC 结果**都不能推广**。测试前必须把它设回 0、`Restart-Service audiosrv`、重验一遍。
4. **必须 `/MANIFEST:NO`**。嵌入 manifest 会调用受保护环境禁止的 API,结果是"有测试键时能跑,键一撤即使 WHQL 签名也加载失败"。
5. **RT 路径禁令**(微软原文):不阻塞、不用分页内存、不调阻塞系统例程。具体到代码就是:`APOProcess` 内禁止 `new`/`delete`/`malloc`、`std::mutex`/`CRITICAL_SECTION`、文件/注册表/COM、日志、`std::string`、异常、**函数内 static**(它的 guard 会加锁)、`Sleep`/`Wait*`。用 `#pragma AVRT_CODE_BEGIN/END` 把 process 路径的代码页锁住,数据用 `AERT_Allocate` 分配。
6. **一个 audiodg 里可能有多个我们的实例**(render + capture + 多端点)。→ **任何全局可变状态都是雷**(Equalizer APO 1.2 就因为 FFTW 并发 bug 在输出+输入设备同时用 GraphicEQ 时崩 audiodg)。FFT plan、scratch buffer、wisdom 表一律 per-instance。
7. **MXCSR 是共享的**。设 FTZ/DAZ 要在 process 入口设、出口恢复——同一条线程上可能还跑着别家的 APO。微软对此**没有任何指导**,这是我们自己的保守选择。

---

## 6. 分阶段路线图

时间按**一个人业余时间**估,单位是周。

### M0 —— 「第一个能出声」:离线 harness(2–3 周)

**里程碑定义:`dspharness.exe in.wav out.wav --preset tube.json` 生成一个听得出胆机味道的 wav。**

这就是第一个能出声的东西,而且它**不碰 audiodg、不碰注册表、不可能把任何人的声音搞没**。

- `dsp/` 静态库骨架 + `Params` + `DspChain` 编排 + 参数平滑 + oversampler
- 先实现两个效果:**压缩器**(最简单、最不可能崩、最容易单测)和**胆机**(最有听感回报、最能体现"比 ViPER 强")
- harness:裸 WAV 读写(不引 libsndfile,PCM16/float32 两种够了)+ 一个 null 测试(全部 bypass 时输出必须与输入**比特相同**)
- 单测:压缩器静态曲线三段对拍手算值;ADAA 在 `Δx→0` 时收敛到直接求值;混响冲激响应能量单调衰减
- 交付物顺便可用:把 harness 挂到 GUI 上做"离线预览",用户能在不装 APO 的情况下听到效果

### M1 —— 两个探针实验(1 周,但可能推翻后面所有设计)

一个 ~250 行、**只做直通 + 写日志**的 APO,不含任何 DSP:

- 注册到本机一个**不常用的**端点(不是默认设备!),验证能加载
- 打日志:`LockForProcess` 收到的 sampleRate / channels / channelMask / **maxFrameCount**;`APOProcess` 每次的 frameCount(记 1000 次看方差)
- **探针 1**:分别试 MFX(`,6`)和 CompositeFX(`,14`),观察 Equalizer APO 的 legacy 槽是否还生效
- **探针 2**:实现 `IAudioSystemEffects3` + `GetApoNotificationRegistrationInfo`,从 Qt 侧写 Volatile 属性存储,看 `HandleNotification` 会不会被调用
- **探针 3**:把 `DisableProtectedAudioDG` 设回 0、重启 audiosrv,看未签名 DLL 还能不能加载
- **同时准备好离线恢复 `.reg` 和 Safe Mode 步骤**——从这一刻起就有把机器搞哑的可能

### M2 —— 生产 APO 外壳(3–4 周)

`CBaseAudioProcessingObject` 派生、格式协商、`LockForProcess` 里做全部分配(`AERT_Allocate` + `VirtualLock`)、AVRT pragma、多实例安全、kill-switch、非 RT 日志。**里面仍然只有直通 + 一个可听的正弦标记音**(用来确认"我们确实在链路里")。自己挂着用一周,不给任何人。

### M3 —— 接第一个真效果(2 周)

`dspcore` 静态链进 APO,只开压缩器。这一步的价值不在功能,在于**验证 M0 里已经跑通的 DSP 在 audiodg 的时序约束下也跑得动**。测 CPU 占用、测各种采样率/声道数、测睡眠唤醒、测切设备。

### M4 —— 参数 IPC + GUI(3–4 周)

管道(或 CAPX,看 M1 结论)+ `DspBridge` + QML 面板。这一步之后产品第一次"像个东西"。

### M5 —— 其余效果(3–4 周)

混响 → 虚拟低音 → 激励器/清晰度 → crossfeed。每加一个都先在 harness 里过,再进 APO。混响是最容易出事的(延迟线、denormal、room size 改变要重开缓冲)。

### M6 —— 安装/看门狗/恢复/卸载(**4–8 周,别低估**)

- 提权 helper:写 HKLM CLSID + FxProperties,**备份被顶掉的原 CLSID**(逻辑自己写,不抄 GPLv2 的 `DeviceAPOInfo.cpp`)
- 看门狗:定时枚举 `HKLM\...\MMDevices\Audio\Render\*`,校验我们的 CLSID 还在不在用户选中的端点上,不在就提示重新注册。**这是 Equalizer APO 十四年来的头号支持负担,不是可选打磨。**
- 健康检查:检测 `PKEY_AudioEndpoint_Disable_SysFx`(`{1da5d803-...},5`)被置 1 并提供一键清除;检测 `DisableProtectedAudioDG` 状态;检测"音频增强"选择器被关;检测空间音频冲突
- 离线恢复:随包发 `unregister.reg` + Safe Mode 文档,放在安装目录根部,文件名要一眼看懂(`没有声音了怎么办.txt`)
- 卸载:恢复被顶掉的 CLSID;**判断 `DisableProtectedAudioDG` 是不是我们设的**(如果是 Equalizer APO 设的就别动);清 `Disable_SysFx`;移除看门狗

**总计约 18–26 周业余时间(300–450 小时)。DSP 只占其中约 20%。** 如果有人告诉你"三个月能做完",他算的是 M0–M3。

---

## 7. 风险与坑(只列真实存在的)

### 会让用户系统没声音的

1. **`APOProcess` 里崩 → audiodg 挂 → 全机所有 app 无声。** 而且不只我们:本机同时有 Equalizer APO(默认端点 pid 1/2)、ViPER4Windows(六个端点的 `,5`,外加一个端点的 `,13`/`,14`)、Bongiovi。我们一个空指针把所有人一起干掉。**缓解**:全部分配在 `LockForProcess`;`APOProcess` 里零分支依赖外部状态;先在 harness 里跑几百万样本的模糊测试;`APOProcess` 外层不要用 SEH 掩盖 bug(掩盖了就变成 Equalizer APO 那种静默旁路),但要有 kill-switch。

2. **10 次失败 → `PKEY_Endpoint_Disable_SysFx = 1` → 整个端点全部系统效果被关。** 计数覆盖 `CoCreateInstance`/`IsInputFormatSupported`/`IsOutputFormatSupported`/`LockForProcess`,成功一次 `LockForProcess` 才清零。**开发期的崩溃循环会把 Equalizer APO 和 ViPER 一起静默关掉,Windows 不给任何解释。** 缓解:开发只在非默认端点上注册;GUI 每次启动检测并提示清除该键。

3. **`DisableProtectedAudioDG` 的所有权耦合。** 本机这个键 = 1 是 **Equalizer APO 的安装器**写的。用户哪天卸载 Equalizer APO,它的卸载器可能清掉这个键 → **我们的未签名 DLL 从此静默不加载,且与用户做的任何事都没有可见因果关系**。缓解:安装时记录"这个键是不是我们设的";健康检查里持续监控它;长期看,搞一个正规 Authenticode 签名(是否够用见 §8)。

4. **写 modern 槽会静默关掉 Equalizer APO 的 legacy 槽。** 微软规则:modern(SFX/MFX/EFX)和 legacy(LFX/GFX)同时配置时,**Windows 用 modern 的**。用户的表现是:**有声音,但 EQ 突然没了**——这是最难排查的一类故障,因为没坏到会去看日志的程度。缓解:M1 探针 1 必须先跑;要么走 CompositeFX 列表共存,要么老老实实 `CoCreateInstance` 把原 APO 当 child 链上。

5. **Windows 功能更新重装音频驱动 → 端点 GUID 变了 → 我们的 FxProperties 蒸发。** 旧端点键变孤儿(显示为已禁用/未连接),新端点键是默认值。23H2/24H2/25H2 都有报告。**这是这条路线最大的长期成本,不是 bug 是常态。** 缓解:看门狗,而且它得是提权的常驻组件。

6. **多实例共享状态。** 一个 audiodg 里可能同时有 render 和 capture 两个我们的实例。Equalizer APO 1.2 就为这个修过 FFTW 并发崩溃。缓解:零全局可变状态,单测里显式跑两个 `DspChain` 并发。

7. **Denormal。** 混响尾巴衰减到 denormal 区,x87/SSE 的 denormal 处理比正常乘法慢 100 倍 → 错过 audiodg 的截止时间 → **所有 app 一起爆音**。缓解:FTZ+DAZ(进出恢复)+ 每条反馈路径注入 1e-20。

### 不会没声音但会挨骂的

8. **独占模式 / ASIO 完全绕过 APO。** foobar2000 WASAPI 独占、多数 DAW、部分游戏。用户会当 bug 报。**必须在 UI 里明说**,而且最好能检测到当前端点被独占并显示提示。

9. **Windows 10+ 不加载 RAW SFX**(加载 RAW MFX);**EFX 对 RAW 流也一律生效**。选槽位时要想清楚。

10. **`audiosrv` 重启会让全机所有 app 瞬断音。** 每次注册变更都要来一次。缓解:批量应用变更;或引导用户禁用/启用端点(影响面小一点)。

11. **控制面板里"音频增强"选择器**必须是"默认/设备默认效果",否则第三方 APO 被绕过。要检测。

12. **空间音频(Windows Sonic / Dolby Atmos)与第三方 APO 冲突。** 要检测并提示。

### 法律 / 许可(这是设计约束,不是脚注)

13. **Equalizer APO 是 GPLv2**(`License.txt` 已确认是 GPL v2 全文)。`DeviceAPOInfo.cpp` 的备份/恢复逻辑、`ClassFactory.cpp`、`FilterEngine.cpp` **一行都不能抄进非 GPLv2 产品**。注册表键名、PID 编号、路径这些是事实,不受版权保护,可以照着重写;代码不行。

14. **EqAPO64-with-VST3 分支是 AGPL/GPLv3**。可以读来理解 out-of-process host 怎么做,不能用。

15. **`likelikeslike/ViPER4Windows` 和 `ViPERDSP` 都是 `license: null`——没有许可证 = 保留所有权利。** brief 里"clone ViPERDSP,可以移植这些文件"的建议是**错的**。而且 ViPERDSP 本身是 `libv4a_fx.so` 反编译产物,再叠一层风险。**只能读来理解,算法从公开文献实现。**

16. **唯一可以逐字抄的参考代码是 Freeverb 的 `tuning.h`——它明确标 PUBLIC DOMAIN**(Jezar @ Dreampoint, 2000)。

17. **MaxxBass 专利 US5930373A 已于 2017-04-04 到期**(不是 brief 说的 2006–2008),状态 "Expired - Lifetime",可以实现。但 **Waves 有没有更晚的延续专利覆盖"谐波支路上的向上压缩器"这个改进,没人查过**;Aphex 的 transient-discriminate 激励器专利 US5424488A 也没人 fetch 过。

18. **绝对不要抄 ViPER4Windows 的私有根 CA 手法**(它的 `Batch\ImportCertificate.cmd` 往受信任根里装 "PureSoftApps Root Certificate 2019")。这是 AV/EDR 红旗,也是对用户的实质安全降级。

### 定位问题

19. **微软的支持模型明确排除我们**:"The new componentized APO design does not allow for an APO to be registered globally and used by multiple different drivers. Each driver must register its own APOs." 我们没有音频驱动,所以只能走 Equalizer APO / ViPER 那条**未受支持但能用**的路。**这一点要原样告诉用户**,别包装成"官方方案"。维护成本是开放式的。

---

## 8. 仍不确定(以及怎么自己验证)

按"决定设计"的重要性排序。前三条**在写安装器代码之前必须解决**。

| # | 不确定的事 | 怎么验证 | 成本 |
|---|---|---|---|
| 1 | **CompositeFX(`,13`/`,14`/`,15`,REG_MULTI_SZ 列表)能不能让我们和 Equalizer APO 在同一端点共存?列表顺序语义是什么?写 CompositeFX 会不会顶掉 legacy LFX/GFX?** 微软只给了一个两 CLSID 的 INF 示例,没说优先级和失败行为。ViPER 在本机用了它,但用在**别的端点**上,不构成共存证明 | M1 探针:在一个闲置端点上,先写 legacy `,1`/`,2` 指向 Equalizer APO 的 CLSID,再把我们的 CLSID 追加进 `,14`,重启 audiosrv,看两个 APO 的日志谁被实例化了、顺序如何 | 半天 |
| 2 | **手工 FxProperties 注册(无 INF)的 APO 能不能用 CAPX?`HandleNotification` 会不会触发?** Default 子存储是 INF 填充的,我们没有 INF | M1 探针:~250 行 APO 实现 `IAudioSystemEffects3` + `GetApoNotificationRegistrationInfo`,Qt 侧 `Activate(IAudioSystemEffectsPropertyStore)` → `OpenVolatilePropertyStore(STGM_READWRITE)` 写一个值,看 APO 日志有没有回调 | 1 天 |
| 3 | **未签名 APO 在 `DisableProtectedAudioDG` 缺失/=0 时到底能不能加载?** 本机的观察全部无效,因为这个键被 Equalizer APO 设成 1 了。微软只正面说了"protected content + 未正确签名 → 失败",没有反面陈述 | 把键设为 0 → `Restart-Service audiosrv` → 看 Equalizer APO(已确认 `NotSigned`)还能不能工作。**顺便回答"普通商业 Authenticode 签名够不够,还是必须 WHQL/PETrust catalog"** ——这决定我们能不能不带注册表 hack 发货 | 半天(有把机器弄哑的风险,先备好恢复 .reg) |
| 4 | **`APOProcess` 实际的 `maxFrameCount` 和每次的 `frameCount`,以及 frameCount 是否恒定** | M1 探针直接打日志。注意:**这个值推不出来**——`GetSharedModeEnginePeriod` 返回的是按格式、按驱动的值(微软示例里 44.1 kHz 是 448 帧默认 / 48 帧最小,不是所谓的 2.67 ms 固定值) | 含在 M1 里 |
| 5 | **CPU 预算**:Dattorro + 多段压缩 + 2× oversampled 饱和 + 虚拟低音,在本机各端点的声道数下,worst case 一个 callback 要多久?离 audiodg 的截止时间还剩多少?**错过截止时间会让所有 app 一起爆音,不只我们** | harness 里跑 `QueryPerformanceCounter`,按 480 帧/块计,和 10 ms(或探针 4 测到的实际周期)对比。留 20% 余量 | 半天,M3 时做 |
| 6 | **Cure+ 到底是什么。** ViPERDSP 有 `Cure.cpp`,本次没读。所有关于它是 crossfeed 的说法都**没有来源** | 读 `https://raw.githubusercontent.com/likelikeslike/ViPERDSP/main/viper/effects/Cure.cpp`。**只读来理解**(该 repo 无许可证) | 1 小时 |
| 7 | **环境模拟(VHE_L0..L4)是什么算法。** 同上,没读 | 读 `viper/effects/VHE*.cpp`。在此之前 UI 上不要承诺"环境模拟"的具体行为 | 1 小时 |
| 8 | **ViPER 的 `noise_sharpening_`(清晰度 Natural)和 `hifi_`(X-HiFi)内部。** 只确认了三个模式是三个不同处理器共用一个增益旋钮,没读实现 | 读 `NoiseSharpening.cpp` 和 hifi 模块 | 2 小时 |
| 9 | **Giannoulis 2013 自动 attack/release 的确切公式。** brief 里的 crest-factor 缩放常数是**推测的重构,不是发表的常数**(原 PDF 两次 TLS 验证失败) | 浏览器打开 `https://www.eecs.qmul.ac.uk/~josh/documents/2013/Giannoulis Massberg Reiss - dynamic range compression automation - JAES 2013.pdf`。在拿到之前,auto attack/release 别上线(auto makeup 有闭式解,可以上) | 1 小时 |
| 10 | **在 `audiodg` 里改 MXCSR(FTZ/DAZ)会不会影响同线程的其他 APO。** 微软对此零指导,进出恢复是我们的保守猜测 | 无法直接验证。做法:严格进出成对恢复,并在探针 APO 里加一个"读取 MXCSR 并记录"的检查,看有没有别家 APO 也在改 | — |
| 11 | **卸载与回滚的完整语义。** 恢复被顶掉的 CLSID、要不要清 `DisableProtectedAudioDG`(怎么知道是谁设的)、清不清 `Disable_SysFx`、看门狗自己怎么卸 | 这是设计工作不是验证工作,但必须在 M6 之前写成文档。半卸载状态是"无法归因的音频故障"的经典来源 | 1 天设计 |
| 12 | **ARM64 是否在范围内。** Equalizer APO 有 `EqualizerAPO-ARM64.rc`,暗示它出原生 ARM64 构建。x64 APO 能不能在 ARM64 的 audiodg 下加载,未知 | 如果目标用户里有 ARM64 笔记本,构建/测试/签名矩阵翻倍。先决定范围再说 | 决策,非验证 |
| 13 | **本机三个第三方 APO(Equalizer APO / ViPER / Bongiovi)同时在时的交互**,child-APO 链式加载能套多深才在格式协商上崩掉 | 只能实测。M1 探针顺带观察 | 含在 M1 |
| 14 | **Equalizer APO 的 `Convolution:` 命令自身加多少延迟、能不能热换 IR。** 这决定"环境模拟 v1 用 APO 卷积"这个省事方案是否成立 | 写一个已知冲激的 IR,用 harness 录 loopback 对齐测延迟;改 IR 文件看重载行为 | 半天 |
| 15 | **`helpers/aeffectx.h` 的 VeSTige 出处和 DMCA 历史**,以及在非 GPL 产品里携带它的法律状态 | **不验证**。VST 路线已因技术原因出局,这个问题不需要解决。如果哪天要重新考虑,**找律师,不是找搜索引擎** | — |
| 16 | **本 brief 中所有引自 `github.com/mirror/equalizerapo` 的 `FilterEngine.cpp` 行号和 10 ms 交叉淡化常数**,是否与 SourceForge 上真正发布的 1.4.2(文件日期 2025-11-28)一致。VST 相关文件已对现行树核对过,FilterEngine 没有 | 从 `https://sourceforge.net/p/equalizerapo/code/ci/main/tree/FilterEngine.cpp?format=raw` 拉一份对拍。这条只影响"config.txt 不会爆音"的论断,不影响架构结论 | 15 分钟 |

---

### 一句话收尾

自己实现这些 DSP **能做**,而且 DSP 本身是整件事里最简单的部分——ViPER 的胆机就是一行 `(acc+x)/2`,我们随手就能做得比它好。真正的工作量、真正的风险、真正会让用户骂街的,全在**注册、共存、更新后重注册、崩溃恢复、卸载**这五件跟音频毫无关系的事上。所以路线是:**先把 DSP 做成一个碰不到 audiodg 的离线库并验证到能出好听的 wav,再花一周跑两个探针实验搞清楚槽位和 CAPX,然后才允许任何代码进 audiodg。**