# DreamDSP 实施方案（Tech Lead 定稿）

> 说明：本方案在 recon brief 之上做了**一次现场复核**。复核发现 `I:\Qt\DreamDSP` 已经存在一个**可编译、可运行**的骨架（`build\dreamdsp\DreamDSP.exe` 存在，1870 行源码），并且 HuskarUI 0.7.0.0 已经被独立构建安装到 `I:\Qt\DreamDSP\third_party\HuskarUI-install`（`HuskarUIConfigVersion.cmake:13 → PACKAGE_VERSION "0.7.0.0"`，`qml/HuskarUI/` 下 `Basic` 与 `Impl` **都在**，`bin/` 下 `HuskarUIBasic.dll` + `HuskarUIImpl.dll` **都在**）。因此**推翻 recon 的 `add_subdirectory` 建议**，改用已实测通过的 `find_package + HuskarUI_DIR 硬指向`。下文所有结论以现场文件为准。

---

## 0. 现状基线（做计划前必须知道的事实）

| 事实 | 证据 |
|---|---|
| HuskarUI 0.7.0.0 已私有安装并可 `find_package` | `I:\Qt\DreamDSP\third_party\HuskarUI-install\lib\cmake\HuskarUI\HuskarUIConfigVersion.cmake:13`；`HuskarUIConfig.cmake:31,37` 导出 `HuskarUI_INCLUDE_DIR` / `HuskarUI_QML_IMPORT_PATH` |
| Qt SDK 里那份 **0.5.2.0 仍在**，且只有 `Basic`、没有 `Impl` | `D:\Qt\6.8.3\msvc2022_64\lib\cmake\HuskarUI\HuskarUIConfigVersion.cmake:13`；`ls D:\Qt\6.8.3\msvc2022_64\qml\HuskarUI` → 仅 `Basic` |
| 工程已能出 exe | `I:\Qt\DreamDSP\build\dreamdsp\DreamDSP.exe`，同目录已 `copy_if_different` 了两个 HuskarUI DLL |
| 里程碑 1 的绝大部分已落地 | `qml/Main.qml`（249 行，`HusWindow` + captionBar + 深色主题 + `HusSelect` 设备下拉 + `ResponseCurveItem` + Repeater 滑块 + 页脚）；`src/app/AppController.{h,cpp}`（debounce 150 ms + `Include:` 接管逻辑） |
| `HusSlider` 的 delegate 拼写是**正确的** `handleDelegate` | `third_party\HuskarUI\src\imports\HusSlider.qml:74 property Component handleDelegate` —— recon 里 doc 的 `handleDelgate` 是文档错字，源码没错 |
| `HusSlider` API 复核通过 | 同文件 `:38 signal firstMoved()`、`:39 signal firstReleased()`、`:45 min`、`:46 max`、`:48 property var value`、`:49 readonly property var currentValue`、`:57 snapMode`、`:58 orientation`、`:69 property HusRadius radiusBg` —— 注意 HusSlider 的 `radiusBg` 是 **HusRadius 对象**，不是 int（recon 说"HusSlider 用 int"是错的，以源码为准） |
| 推荐图标枚举名全部存在 | `src\cpp\controls\husiconfont.h`：`SlidersOutlined=0xecac:954`、`SoundOutlined:971`、`SaveOutlined:897`、`FolderOpenOutlined:499`、`ReloadOutlined:860`、`SettingOutlined:919`、`LineChartOutlined:646`、`PoweroffOutlined:796`、`ThunderboltOutlined:1027`、`MoonOutlined:719`、`SunOutlined:993`、`ImportOutlined:595`、`ExportOutlined:385`、`DeleteOutlined:291`、`PlusOutlined:784` |

**结论：不是从零开始，是从"能跑的 demo"推进到"能替代 Peace 的产品"。** 计划按此定。

---

## 1. 最终架构

### 1.1 目录树（精确到文件名）

```
I:\Qt\DreamDSP\
├─ CMakeLists.txt                  ← 顶层，见 §2
├─ .gitignore
├─ README.md
├─ scripts\
│   ├─ env.bat                     ← QT_DIR / VCVARS / NINJA（已存在，需改 VS2026 路径）
│   ├─ build-huskarui.bat          ← 构建并安装 HuskarUI 0.7.0.0 到 third_party\HuskarUI-install
│   ├─ build.bat                   ← 配置+构建 DreamDSP（HuskarUI_DIR 硬指向）
│   ├─ run.bat
│   └─ deploy.bat                  ← 【新增】windeployqt + 手工补 HuskarUI*.dll
│
├─ third_party\
│   ├─ HuskarUI\                   ← git clone，只读，不改一行
│   └─ HuskarUI-install\           ← 构建产物（bin/ lib/ include/ qml/），**受版本控制忽略**
│
├─ src\
│   ├─ main.cpp                    ← QApplication + OpenGL + alphaBuffer + 前置 import path
│   │
│   ├─ apocfg\                     ← 【lib target: dreamdsp_apocfg】纯 Qt Core，无 GUI，无 QML
│   │   ├─ ApoNum.h/.cpp           ← Num（text+value）、getFreq 的 ×1000 陷阱、C-locale 格式化
│   │   ├─ ApoTokens.h/.cpp        ← FilterToken 18 项、参数表（gainRequired/defaultQ/usesCornerFreq）
│   │   ├─ ApoCommands.h           ← PreampCmd/BiquadCmd/IirCmd/.../LoudnessCorrectionCmd + std::variant
│   │   ├─ ApoLine.h/.cpp          ← 单行：raw + kind + key + value + cmd + dirty；render()
│   │   ├─ ApoDocument.h/.cpp      ← 行列表模型；load/serialise/saveAtomic/roundTripsExactly
│   │   ├─ ApoParser.h/.cpp        ← 15 个 factory，顺序即 APO 的优先级
│   │   ├─ ApoEmitter.h/.cpp       ← 每个 Command 的规范化文本
│   │   ├─ ApoChannels.h/.cpp      ← 9 个标准名 + SUB/SL/SR 别名解析
│   │   └─ ApoEnv.h/.cpp           ← 注册表 ConfigPath/InstallPath、Include 相对路径解析
│   │
│   ├─ peacefmt\                   ← 【lib target: dreamdsp_peacefmt】Peace 1.6.9.11 互操作
│   │   ├─ PeaceIni.h/.cpp         ← 手写 INI（**禁止 QSettings**），行保序、首个 '=' 切分
│   │   ├─ PeacePreset.h/.cpp      ← .peace 模型：Speaker/Band/Effects/Surround/Upmix/Midside/Commands
│   │   ├─ PeaceSettings.h/.cpp    ← peace.ini [General]/[GUI]/[Changing Sliders]/[Export]/...
│   │   ├─ PeaceEmitter.h/.cpp     ← 5-bucket peace.txt 渲染（含 BW/LR 级联展开）
│   │   └─ PeaceFilterTypes.h      ← 18 项 FilterTypes 表（PK..HSQ）
│   │
│   ├─ platform\                   ← 【lib target: dreamdsp_platform】Win32/COM，无 QML
│   │   ├─ ApoLocator.h/.cpp       ← HKLM64\SOFTWARE\EqualizerAPO（已存在）
│   │   ├─ AudioDevices.h/.cpp     ← IMMDeviceEnumerator，4 级 friendly-name 回退（已存在，需扩）
│   │   ├─ ApoDeviceStatus.h/.cpp  ← FxProperties pid{1,2,5,6,7,13,14,15}，REG_MULTI_SZ，GUID 比较
│   │   ├─ EndpointNotifier.h/.cpp ← IMMNotificationClient → Qt signal（queued marshalling）
│   │   ├─ AtomicWriter.h/.cpp     ← temp→MoveFileEx，ERROR_SHARING_VIOLATION 重试
│   │   ├─ SafeFileWatcher.h/.cpp  ← QFileSystemWatcher 重挂载封装
│   │   ├─ HotkeyManager.h/.cpp    ← RegisterHotKey(nullptr,...) + QAbstractNativeEventFilter
│   │   └─ PeakMeter.h/.cpp        ← IAudioMeterInformation，30 Hz
│   │
│   ├─ core\                       ← 【lib target: dreamdsp_core】DSP，纯算法
│   │   ├─ Biquad.h/.cpp           ← RBJ cookbook 设计 + magnitudeDb（已存在）
│   │   └─ ResponseCalc.h/.cpp     ← 【新增】多段串联响应、log 采样、缓存
│   │
│   ├─ app\                        ← 【进 exe target，QML_ELEMENT】
│   │   ├─ AppController.h/.cpp    ← QML_SINGLETON 门面（已存在）
│   │   ├─ EqBandModel.h/.cpp      ← QAbstractListModel，31 行（已存在）
│   │   ├─ SpeakerModel.h/.cpp     ← 【新增】speaker/通道组模型
│   │   ├─ PresetModel.h/.cpp      ← 【新增】.peace 预设列表
│   │   ├─ DeviceModel.h/.cpp      ← 【新增】设备列表（含 APO 是否接管标记）
│   │   └─ SettingsStore.h/.cpp    ← 【新增】DreamDSP 自己的 QSettings（键名无空格无斜杠）
│   │
│   └─ ui\
│       └─ ResponseCurveItem.h/.cpp ← QQuickPaintedItem（已存在）
│
├─ qml\
│   ├─ Main.qml                    ← HusWindow 根 + captionBar + 左侧 HusMenu + StackLayout
│   ├─ pages\
│   │   ├─ EqualizerPage.qml       ← 曲线 + 滑块阵列 + 通道页签
│   │   ├─ FiltersPage.qml         ← HusTableView 精确参数编辑
│   │   ├─ PresetsPage.qml         ← .peace 浏览/导入/导出
│   │   ├─ RoutingPage.qml         ← Copy/Routing 矩阵
│   │   ├─ RawConfigPage.qml       ← 生成文本预览 + config.txt 只读视图
│   │   └─ SettingsPage.qml        ← 主题/强调色/热键/托盘
│   ├─ components\
│   │   ├─ BandStrip.qml           ← 单段：dB 读数 + 竖 HusSlider + 频率标签（已存在）
│   │   ├─ SectionCard.qml         ← 圆角卡片（已存在）
│   │   ├─ StatusPill.qml          ← 状态胶囊（已存在）
│   │   ├─ MeterBar.qml            ← 【新增】峰值表
│   │   └─ ParamField.qml          ← 【新增】HusInputNumber 封装（Hz/dB/Q 三态）
│   └─ Theme.qml                   ← 【新增】QML singleton，集中 token 换算（parseInt 那一堆）
│
└─ tests\
    ├─ CMakeLists.txt
    ├─ tst_apocfg_roundtrip.cpp    ← 对 D:\Program Files\EqualizerAPO\config\*.txt 全量 byte-exact
    ├─ tst_peaceini_roundtrip.cpp  ← 对 peace.ini + 25 个 *.peace 全量 byte-exact
    └─ tst_emitter_golden.cpp      ← 与 Peace 生成的 peace.txt 逐字节 diff
```

### 1.2 依赖方向（严格单向，不许回指）

```
                 ┌──────────────┐
                 │   qml/*      │  (QML 前端)
                 └──────┬───────┘
                        │ 只通过 QML_SINGLETON / QAbstractListModel / QQuickPaintedItem
                 ┌──────▼───────┐
                 │   src/app    │  (门面层，唯一知道"用户意图"的地方)
                 └──┬───┬───┬───┘
        ┌───────────┘   │   └───────────┐
   ┌────▼─────┐   ┌─────▼─────┐   ┌─────▼──────┐
   │ apocfg   │   │ peacefmt  │   │  platform  │
   └────┬─────┘   └─────┬─────┘   └────────────┘
        │               │
        └──────┬────────┘
          ┌────▼────┐
          │  core   │  (Biquad / ResponseCalc，零依赖)
          └─────────┘
```

- `apocfg` ↛ `peacefmt`（APO 语法不知道 Peace 的存在）
- `peacefmt` → `apocfg`（Peace 的 emitter 产出 APO 文本，复用 apocfg 的 emitter 保证格式一致）
- `platform` ↛ 任何上层（只暴露 struct + signal）
- `app` 是**唯一**允许同时 include 三者的层
- 任何 `src/apocfg`、`src/peacefmt`、`src/platform`、`src/core` 的头文件里**不许出现 `QML_ELEMENT`**——它们要能被 `tests/` 直接链接、无 QML engine 跑单测。

### 1.3 QML / C++ 边界画在哪里

**C++ 侧负责（QML 一行都不碰）：**
1. 文件 I/O、字节级 round-trip、原子写、注册表、COM。
2. 一切 `double` 的格式化。QML 的 `Number.toFixed()` **不可用于生成配置文本**——它跟随 JS 的舍入，且不保证 C locale。所有落盘数字必须 `QString::asprintf("%.1f", v)`。
3. DSP 响应计算（`ResponseCalc`），QML 只拿到一个 `QQuickPaintedItem` 的属性。
4. 防抖 / 节流 / 竞态。QML 里不许出现 `Timer` 来做写文件节流。

**QML 侧负责：**
1. 布局、动画、主题 token 绑定、控件组合。
2. 交互语义（哪个滑块对应哪个 band index）。
3. **不持有状态**。所有可持久化的值都是 `AppController.*` 或 model role 的 binding。

**边界上的三种通道（只有这三种，不许加 context property）：**
| 通道 | 用于 | 例子 |
|---|---|---|
| `QML_SINGLETON` 门面 | 全局标量 + 命令 | `AppController.preamp`、`AppController.flushNow()` |
| `QAbstractListModel` | 重复项 | `AppController.bands`（31 行）、`presets`、`devices` |
| `QQuickPaintedItem` | 高频重绘 | `ResponseCurveItem` |

**反向通道只有 signal**：`writeFailed(QString)`、`externalConfigChanged()`、`deviceListChanged()`。**不许用返回值**报错，因为 QML 无法可靠处理异步返回码。

---

## 2. CMake 方案

### 2.1 消费 HuskarUI：`find_package` + `HuskarUI_DIR` 硬指向（**实测可行，已出 exe**）

**不用 `add_subdirectory` 的理由（全部实证）：**
1. `add_subdirectory` 后链接名是裸的 `HuskarUIBasic`，且 `HuskarUI::Basic` 这个 alias 在构建树里不存在（`src/CMakeLists.txt:189-197` 只设了 `EXPORT_NAME "Basic"`，export 是 install 时才生成的）。以后想切成 install 流程要改一堆名字。
2. `add_subdirectory` 会把 HuskarUI 的 `INSTALL_HUSKARUI_IN_DEFAULT_LOCATION`（`src/CMakeLists.txt:54-58`）和 gallery 的 `QML_IMPORT_PATH`（`gallery/CMakeLists.txt:202-208`）用 `FORCE` 写进你的 cache。要靠 4 个 `set(... CACHE ... FORCE)` 去压制它，脆。
3. HuskarUI 编译很慢（含 QWindowKit + shader），每次 clean build 都陪跑一遍不划算。
4. **决定性理由**：`third_party\HuskarUI-install` 已经存在且完整（`Basic` + `Impl` + 两个 DLL），`scripts\build-huskarui.bat` 已经把这条路跑通了。

**必须做的一件事**：`find_package(HuskarUI ...)` **绝不能裸调用**——`D:\Qt\6.8.3\msvc2022_64\lib\cmake\HuskarUI` 里那份 0.5.2.0 在 `CMAKE_PREFIX_PATH` 上，会被优先命中。用 `-DHuskarUI_DIR=` 直接指向私有安装（`scripts\build.bat` 已这么做），并在 CMakeLists 里加**版本断言**兜底。

### 2.2 顶层 `CMakeLists.txt`（完整、可直接替换现有文件）

```cmake
cmake_minimum_required(VERSION 3.21)

project(DreamDSP VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Widgets 是强制的：Qt.labs.platform 的 SystemTrayIcon 硬依赖 Qt6::Widgets
# 证据: D:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6LabsPlatform/Qt6LabsPlatformDependencies.cmake:50
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick Widgets)

# ---------------------------------------------------------------------------
# HuskarUI：必须命中 third_party/HuskarUI-install 里的 0.7.0.0。
# Qt SDK 里还躺着一份 0.5.2.0（只有 Basic、没有 Impl），一旦命中，
# HusWindow.qml:25 的 `import HuskarUI.Impl` 会在运行时炸。
# 由 scripts/build.bat 传 -DHuskarUI_DIR=<repo>/third_party/HuskarUI-install/lib/cmake/HuskarUI
# ---------------------------------------------------------------------------
if(NOT DEFINED HuskarUI_DIR)
    set(HuskarUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/HuskarUI-install/lib/cmake/HuskarUI"
        CACHE PATH "HuskarUI package config directory")
endif()

# 版本区间 pin：HuskarUIConfigVersion 用 SameMinorVersion 比较
# (third_party/HuskarUI/src/CMakeLists.txt:277-281)，0.5.2.0 会被正确拒绝。
find_package(HuskarUI 0.7 REQUIRED NO_DEFAULT_PATH PATHS "${HuskarUI_DIR}")

if(NOT HuskarUI_VERSION VERSION_GREATER_EQUAL 0.7)
    message(FATAL_ERROR
        "Found HuskarUI ${HuskarUI_VERSION} at ${HuskarUI_DIR}. "
        "DreamDSP requires >= 0.7.0.0 (HusWindow needs the HuskarUI.Impl module). "
        "Run scripts\\build-huskarui.bat first.")
endif()
message(STATUS "DreamDSP: HuskarUI ${HuskarUI_VERSION} @ ${HuskarUI_DIR}")

# HuskarUIConfig.cmake:31/:37 导出这两个变量；运行时 DLL 在 <prefix>/bin
get_filename_component(HUSKARUI_ROOT "${HuskarUI_INCLUDE_DIR}" DIRECTORY)
set(HUSKARUI_BIN_DIR "${HUSKARUI_ROOT}/bin")

# QTP0001 → RESOURCE_PREFIX 默认 "/qt/qml/"，而 qrc:/qt/qml 自 Qt 6.5 起
# 已在引擎默认 import path 上 → 自己的模块无需任何 addImportPath。
qt_standard_project_setup(REQUIRES 6.8)

# ===========================================================================
# 静态库：可被 tests/ 直接链接，且完全不含 QML 类型
# ===========================================================================
add_library(dreamdsp_core STATIC
    src/core/Biquad.h            src/core/Biquad.cpp
    src/core/ResponseCalc.h      src/core/ResponseCalc.cpp
)
target_include_directories(dreamdsp_core PUBLIC src)
target_link_libraries(dreamdsp_core PUBLIC Qt6::Core)

add_library(dreamdsp_apocfg STATIC
    src/apocfg/ApoNum.h          src/apocfg/ApoNum.cpp
    src/apocfg/ApoTokens.h       src/apocfg/ApoTokens.cpp
    src/apocfg/ApoCommands.h
    src/apocfg/ApoLine.h         src/apocfg/ApoLine.cpp
    src/apocfg/ApoDocument.h     src/apocfg/ApoDocument.cpp
    src/apocfg/ApoParser.h       src/apocfg/ApoParser.cpp
    src/apocfg/ApoEmitter.h      src/apocfg/ApoEmitter.cpp
    src/apocfg/ApoChannels.h     src/apocfg/ApoChannels.cpp
    src/apocfg/ApoEnv.h          src/apocfg/ApoEnv.cpp
)
target_include_directories(dreamdsp_apocfg PUBLIC src)
target_link_libraries(dreamdsp_apocfg PUBLIC Qt6::Core PRIVATE advapi32)

add_library(dreamdsp_peacefmt STATIC
    src/peacefmt/PeaceIni.h        src/peacefmt/PeaceIni.cpp
    src/peacefmt/PeacePreset.h     src/peacefmt/PeacePreset.cpp
    src/peacefmt/PeaceSettings.h   src/peacefmt/PeaceSettings.cpp
    src/peacefmt/PeaceEmitter.h    src/peacefmt/PeaceEmitter.cpp
    src/peacefmt/PeaceFilterTypes.h
)
target_include_directories(dreamdsp_peacefmt PUBLIC src)
target_link_libraries(dreamdsp_peacefmt PUBLIC Qt6::Core dreamdsp_apocfg)

add_library(dreamdsp_platform STATIC
    src/platform/ApoLocator.h       src/platform/ApoLocator.cpp
    src/platform/AudioDevices.h     src/platform/AudioDevices.cpp
    src/platform/ApoDeviceStatus.h  src/platform/ApoDeviceStatus.cpp
    src/platform/EndpointNotifier.h src/platform/EndpointNotifier.cpp
    src/platform/AtomicWriter.h     src/platform/AtomicWriter.cpp
    src/platform/SafeFileWatcher.h  src/platform/SafeFileWatcher.cpp
    src/platform/HotkeyManager.h    src/platform/HotkeyManager.cpp
    src/platform/PeakMeter.h        src/platform/PeakMeter.cpp
)
target_include_directories(dreamdsp_platform PUBLIC src)
target_link_libraries(dreamdsp_platform PUBLIC Qt6::Core Qt6::Gui
    PRIVATE ole32 oleaut32 propsys advapi32 user32 version)

foreach(t dreamdsp_core dreamdsp_apocfg dreamdsp_peacefmt dreamdsp_platform)
    target_compile_definitions(${t} PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endforeach()

# ===========================================================================
# 可执行 + QML 模块
# ===========================================================================
qt_add_executable(DreamDSP
    src/main.cpp
)

qt_add_qml_module(DreamDSP
    URI DreamDSP
    VERSION 1.0
    # 不写 RESOURCE_PREFIX：走 QTP0001 的 "/qt/qml/"，配合 loadFromModule()
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/pages/EqualizerPage.qml
        qml/pages/FiltersPage.qml
        qml/pages/PresetsPage.qml
        qml/pages/RoutingPage.qml
        qml/pages/RawConfigPage.qml
        qml/pages/SettingsPage.qml
        qml/components/BandStrip.qml
        qml/components/SectionCard.qml
        qml/components/StatusPill.qml
        qml/components/MeterBar.qml
        qml/components/ParamField.qml
    RESOURCES
        qml/assets/tray.png
    # 所有带 QML_ELEMENT 的 .h 必须列在 SOURCES 里，qmltyperegistrar 才看得到
    SOURCES
        src/app/AppController.h    src/app/AppController.cpp
        src/app/EqBandModel.h      src/app/EqBandModel.cpp
        src/app/SpeakerModel.h     src/app/SpeakerModel.cpp
        src/app/PresetModel.h      src/app/PresetModel.cpp
        src/app/DeviceModel.h      src/app/DeviceModel.cpp
        src/app/SettingsStore.h    src/app/SettingsStore.cpp
        src/ui/ResponseCurveItem.h src/ui/ResponseCurveItem.cpp
)

# qmltyperegistrar 生成的 .cpp 用 `#include <AppController.h>`（裸 basename，
# 且被 __has_include 包着）——任何存放 QML_ELEMENT 头文件的目录都必须在
# include path 上，否则该类型会"静默地"注册失败，运行时才报 unknown type。
target_include_directories(DreamDSP PRIVATE src src/app src/ui)

target_compile_definitions(DreamDSP PRIVATE
    HUSKARUI_IMPORT_PATH="${HuskarUI_QML_IMPORT_PATH}"
    DREAMDSP_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(DreamDSP PRIVATE
    Qt6::Quick
    Qt6::Widgets            # Qt.labs.platform SystemTrayIcon
    HuskarUI::Basic
    dreamdsp_core
    dreamdsp_apocfg
    dreamdsp_peacefmt
    dreamdsp_platform
)

set_target_properties(DreamDSP PROPERTIES WIN32_EXECUTABLE TRUE)

# 让 Qt Creator / qmlls 找到 HuskarUI 的类型信息
set(QML_IMPORT_PATH "${HuskarUI_QML_IMPORT_PATH}" CACHE STRING "" FORCE)

# HuskarUI 是 shared build：两个 DLL 必须落在 exe 旁，否则直接跑构建树会
# 找不到（windeployqt 也不会替你拷，实测只拷 huskaruibasicplugin.dll）。
add_custom_command(TARGET DreamDSP POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${HUSKARUI_BIN_DIR}/HuskarUIBasic.dll"
        "${HUSKARUI_BIN_DIR}/HuskarUIImpl.dll"
        "$<TARGET_FILE_DIR:DreamDSP>"
    COMMENT "Copying HuskarUI runtime next to DreamDSP.exe")

# ===========================================================================
enable_testing()
add_subdirectory(tests)
```

### 2.3 `main.cpp` 的三处必改（当前版本有真实缺陷）

现有 `src/main.cpp` 已经对了两件关键事（`setGraphicsApi(OpenGL)`、`setDefaultAlphaBuffer(true)`、**prepend** import path 而不是 append —— 这一点很关键，因为 Qt SDK 里那份 0.5.2.0 在默认 import path 上，`addImportPath()` 只会追加到尾部、压不住它）。要改的：

```cpp
// 1) QGuiApplication → QApplication（托盘需要 Qt6::Widgets）
#include <QApplication>
QApplication app(argc, argv);
QApplication::setQuitOnLastWindowClosed(false);   // 关窗即最小化到托盘

// 2) engine.load(QUrl("qrc:/DreamDSP/qml/Main.qml"))
//    → 去掉 RESOURCE_PREFIX "/" 之后改用：
engine.loadFromModule("DreamDSP", "Main");

// 3) 退出前强制 flush，别丢最后 150 ms 的编辑
QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
    dreamdsp::AppController::instance()->flushNow();
});
```

### 2.4 Ninja + MSVC 配置命令行

```bat
:: 一次性：VS 2026 环境（注意 scripts\env.bat 现在指向 VS2022，需要改）
call "D:\Program Files\VisualStudio\2026\IDE\VC\Auxiliary\Build\vcvars64.bat"

:: 步骤 1：构建并安装 HuskarUI 0.7.0.0（只做一次，除非升级 HuskarUI）
cmake -G Ninja -S I:\Qt\DreamDSP\third_party\HuskarUI ^
      -B I:\Qt\DreamDSP\build\huskarui ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_MAKE_PROGRAM=D:\Qt\Tools\Ninja\ninja.exe ^
      -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64 ^
      -DCMAKE_INSTALL_PREFIX=I:/Qt/DreamDSP/third_party/HuskarUI-install ^
      -DBUILD_HUSKARUI_GALLERY=OFF ^
      -DBUILD_HUSKARUI_STATIC_LIBRARY=OFF ^
      -DBUILD_HUSKARUI_ON_DESKTOP_PLATFORM=ON ^
      -DINSTALL_HUSKARUI_IN_DEFAULT_LOCATION=OFF
cmake --build I:\Qt\DreamDSP\build\huskarui --parallel
cmake --install I:\Qt\DreamDSP\build\huskarui

:: 步骤 2：DreamDSP
cmake -G Ninja -S I:\Qt\DreamDSP -B I:\Qt\DreamDSP\build\dreamdsp ^
      -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DCMAKE_MAKE_PROGRAM=D:\Qt\Tools\Ninja\ninja.exe ^
      -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64 ^
      -DHuskarUI_DIR=I:/Qt/DreamDSP/third_party/HuskarUI-install/lib/cmake/HuskarUI
cmake --build I:\Qt\DreamDSP\build\dreamdsp --parallel
```

**为什么是 `RelWithDebInfo` 不是 `Debug`**：HuskarUI 装的是 Release（`/MD`）。Qt 的 QML plugin 加载会因 CRT/Qt flavour 不匹配而拒绝加载 Debug 版本混 Release 插件。`RelWithDebInfo` 用 release runtime 且有调试符号。`scripts\build.bat` 里已经写了这条注释，是对的，保留。

**待验证**：`scripts\env.bat` 里 `VCVARS` 硬编码为 `D:\Program Files\VisualStudio\2022\...`，但 recon 里另一条证据显示本机 MSVC 是 `D:\Program Files\VisualStudio\2026\IDE\VC\Tools\MSVC\14.51.36231`。**上手第一件事就是核对这个路径并改 env.bat**。

---

## 3. 第一个里程碑的确切范围

### 3.1 定义

**M1 = "一个能跑起来的、现代观感的窗口"**：`HusWindow` 无边框 + 深色主题 + 设备下拉 + 10 段 EQ 滑块 + 响应曲线 + 实时写文件。

**M1 的写入目标不是 `peace.txt`，是 `dreamdsp.txt`。** 这是我要推翻任务描述里"实时写 peace.txt"这句的地方，理由是硬的：

- Peace 1.6.9.11 **就装在同一个 config 目录里**，且它自己也写 `peace.txt`（`Peace.au3:377 $CommandsFile = "peace.txt"`）。两个进程抢同一个文件必然打架。
- 用户当前的 `config.txt` 只有一行 `Include: peace.txt`，`peace.txt` 现在是 **0 字节**（Peace 处于 `OnOff=0`）。
- 正确做法：DreamDSP 写自己的 `dreamdsp.txt`，并对 `config.txt` 做**最小 read-modify-write**——只保证 `Include: dreamdsp.txt` 这一行存在/不存在，其余行一个字节不动。现有 `AppController::setEngaged()` 已经是这么实现的（`src/app/AppController.cpp:80-107`），**保持**。

### 3.2 M1 已完成（复核确认，不用重做）

| 文件 | 状态 |
|---|---|
| `CMakeLists.txt` | ✅ 能配能构，但需按 §2.2 升级（加 Widgets / 拆库 / 版本断言） |
| `src/main.cpp` | ✅ 65 行，OpenGL + alphaBuffer + prepend import path 都对；需按 §2.3 改 3 处 |
| `src/platform/ApoLocator.{h,cpp}` | ✅ 96 行，注册表定位 + 版本 + 可写性 |
| `src/platform/AudioDevices.{h,cpp}` | ✅ 119 行，基础枚举 |
| `src/core/Biquad.{h,cpp}` | ✅ 162 行，RBJ 设计 + magnitudeDb + apoToken |
| `src/core/ApoConfig.{h,cpp}` | ✅ 127 行，行列表读写 + Include 增删 |
| `src/app/EqBandModel.{h,cpp}` | ✅ 121 行，5 个 role（含 `LabelRole` 预格式化） |
| `src/app/AppController.{h,cpp}` | ✅ 223 行，150 ms debounce + generatedText |
| `src/ui/ResponseCurveItem.{h,cpp}` | ✅ 208 行 |
| `qml/Main.qml` + 3 个 component | ✅ 249 + 157 行 |

### 3.3 M1 剩余 delta（要动的每一个文件 + 内容要点）

**① `src/app/AppController.cpp` —— 修 3 个真实缺陷**

- **构造函数里无条件 `writeNow()`**（`AppController.cpp:37-38`）。这会在**每次启动**时往 APO 的被监视目录写一个文件，触发一次无意义的全量 reload（APO 用 `FindFirstChangeNotificationW(configPath, bWatchSubtree=TRUE, FILE_NOTIFY_CHANGE_FILE_NAME|LAST_WRITE)`）。改成：只在 `dreamdsp.txt` **不存在**时创建，且创建空内容。
- **`Device:` 行直接用 friendly name**（`AppController.cpp:130-133`）。APO 的 Device 匹配是**大小写不敏感的子串匹配**，模式按 `;` 分候选、按空格分词，**同一候选的所有词都必须是 `"DeviceName ConnectionName GUID"` 的子串**（`DeviceFilterFactory.cpp:78-135`）。中文设备名（本机是 `后面板 耳机 (Realtek USB Audio)`）里的括号会被当成普通字符参与匹配，能过，但**含 `;` 的名字会被劈成两个候选**。必须做转义/降级：优先写 GUID 形式（模式词含 `{` 时 GUID 不被从 haystack 里剥掉），并提供"全部设备"选项时**不写 Device 行**。
- **`writeNow()` 目前走 `ApoConfig::writeLines`**，需要替换为 `platform/AtomicWriter`：temp 文件放在 `<InstallPath>\.dreamdsp-tmp\`（同卷、在被监视目录之外），再 `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)`，对 `ERROR_SHARING_VIOLATION`/`ERROR_LOCK_VIOLATION`/`ERROR_ACCESS_DENIED` 做退避重试（前 10 次 2 ms，之后 20 ms，上限 ~1 s）。**不许传 `MOVEFILE_COPY_ALLOWED`**（跨卷时会退化成非原子的 copy+delete）。

**② `qml/components/BandStrip.qml` —— 修滑块双向绑定**

现在 `qml/Main.qml:151-158` 的 preamp 滑块用的是：
```qml
value: AppController.preamp
onValueChanged: { if (Math.abs(value - AppController.preamp) > 0.001) AppController.preamp = value; }
```
这是错的两点：(a) 在 `onValueChanged` 里写回会**破坏 `value` 上的 binding**（QML 的 binding 一旦被命令式赋值就断开）；(b) `HusSlider.value` 是 `property var`（`HusSlider.qml:48`），拖动时引擎内部更新的是 `currentValue`（`:49` readonly）。正确写法：

```qml
HusSlider {
    orientation: Qt.Vertical
    min: -30; max: 10; stepSize: 0.1
    snapMode: HusSlider.SnapAlways
    value: AppController.preamp          // 单向：C++ → QML
    onFirstMoved:    AppController.preamp = currentValue   // 拖动中，高频
    onFirstReleased: AppController.preamp = currentValue   // 松手，确保终值
}
```

**③ `qml/Main.qml` —— 修 HusToolTip 的自引用**

`Main.qml:98-102` 写了 `HusToolTip { parent: parent; visible: parent.hovered; ... }`。`parent: parent` 在自身作用域里 `parent` 先解析为 HusToolTip 自己的 parent，语义混乱。库自己的写法（`gallery/qml/Gallery.qml:80-85`）是**不写 `parent:`，只写 `visible: parent.hovered`**，把 tooltip 直接嵌在被 hover 的控件里。照抄。

**④ 新增 `qml/Theme.qml`（QML singleton）**

HuskarUI 的 token 有一半是**字符串**（库自己在 `src/imports/HusText.qml:36` 里 `parseInt(HusTheme.Primary.fontPrimarySize)`）。把换算集中掉，避免全工程散落 `parseInt`：

```qml
pragma Singleton
import QtQuick
import HuskarUI.Basic

QtObject {
    readonly property int  radiusLg   : parseInt(HusTheme.Primary.radiusPrimaryLG)
    readonly property int  radius     : parseInt(HusTheme.Primary.radiusPrimary)
    readonly property int  durFast    : parseInt(HusTheme.Primary.durationFast)
    readonly property int  durMid     : parseInt(HusTheme.Primary.durationMid)
    readonly property int  fsBody     : parseInt(HusTheme.Primary.fontPrimarySize)
    readonly property int  fsH5       : parseInt(HusTheme.Primary.fontPrimarySizeHeading5)
    readonly property color grid      : HusTheme.isDark ? Qt.rgba(1,1,1,0.10) : Qt.rgba(0,0,0,0.10)
    readonly property color axisLabel : HusTheme.isDark ? Qt.rgba(1,1,1,0.45) : Qt.rgba(0,0,0,0.45)
}
```
（需要在 `qml/` 下加一行 `singleton Theme 1.0 Theme.qml` 的 qmldir？**不需要** —— `qt_add_qml_module` 会自动从 `pragma Singleton` 生成 qmldir 条目。）

**⑤ `qml/Main.qml` —— 加 Mica，带降级**

```qml
Component.onCompleted: {
    HusTheme.darkMode = HusTheme.Dark
    HusTheme.installThemePrimaryColorBase('#3d7eff')
    HusTheme.textRenderType = HusTheme.NativeRendering   // OpenGL 下小字更锐
    win.setWindowMode(true)
    if (!win.setSpecialEffect(HusWindow.Win_Mica))       // 返回 bool
        win.setSpecialEffect(HusWindow.None)             // 失败回落，恢复 colorBgBase
}
```
**待验证**：`setSpecialEffect(Win_Mica)` 在 Windows 11 build 26200 上的实际返回值没人跑过。必须检查返回值，不能假设成功。

**⑥ 新增 `tests/tst_apocfg_roundtrip.cpp` 的最小版**

M1 阶段只测一件事：`ApoConfig::readLines` + `writeLines` 对 `D:\Program Files\EqualizerAPO\config\config.txt`、`configbeforePeace.txt`、`demo.txt`、`example.txt`、`multichannel.txt`、`iir_lowpass.txt`、`selective_delay.txt` 做 load→save，**字节完全一致**。`configbeforePeace.txt:4` 那行孤零零的 `-1.8`（无冒号）是最好的金丝雀。

### 3.4 M1 验收标准

1. `scripts\build.bat` 从干净树一把过，无 warning（`linktarget` 那条 CMake warning 除外，见 §6）。
2. 窗口打开：无边框、深色、Mica 或干净的 `colorBgBase` 兜底、圆角卡片、拖动标题栏可移动、三个系统按钮工作。
3. 设备下拉里出现 `后面板 耳机 (Realtek USB Audio)` 并标记为默认设备。
4. 拖 10 根滑块，曲线跟手无卡顿。
5. 松手后 150 ms，`D:\Program Files\EqualizerAPO\config\dreamdsp.txt` 出现正确内容，UTF-8 **无 BOM**、CRLF。
6. 点"接管"后 `config.txt` 从 1 行变 2 行，原来的 `Include: peace.txt` **原封不动**；点"停止接管"后恢复成 1 行、**字节完全一致**。
7. 单测全绿。

---

## 4. libapocfg 的 C++ 设计

### 4.1 核心不变式（一句话，其余全是它的推论）

> **文档是一个"原始行的列表"，语义解析只是叠加在行上的 overlay。序列化 = 对每一行输出 `dirty ? render(cmd) : raw`。绝不从语义模型重新生成整个文件。**

这条不是我的发明，是 APO 官方 Editor 的做法（`Editor/MainWindow.cpp:286-302` 把文件读成 `QList<QString> lines`，`:317-333` 把这些 raw line 用 `"\r\n"` join 回去）。任何"解析成 AST 再打印"的设计都会在 `configbeforePeace.txt:4` 的 `-1.8` 上死掉。

### 4.2 类图

```
                    ┌─────────────────────────────────────────┐
                    │            ApoDocument                  │
                    │  QString filePath                       │
                    │  QVector<ApoLine> lines                 │
                    │  Eol eol; bool hadBom, wasAnsiFallback, │
                    │            trailingNewline              │
                    │  + load(path) -> ApoDocument            │
                    │  + serialise() -> QByteArray            │
                    │  + saveAtomic() -> bool                 │
                    │  + roundTripsExactly(orig) -> bool      │
                    └────────────────┬────────────────────────┘
                                     │ 1..*
                    ┌────────────────▼────────────────────────┐
                    │              ApoLine                    │
                    │  QString raw          ← 权威（!dirty）  │
                    │  LineKind kind                          │
                    │  QString key, value; int colonPos       │
                    │  ApoCommand cmd       ← 权威（dirty）   │
                    │  bool dirty, hasInlineExpression        │
                    │  + render() const -> QString            │
                    └───────┬────────────────────┬────────────┘
                            │ uses               │ variant
              ┌─────────────▼────────┐   ┌───────▼──────────────────────────┐
              │     ApoParser        │   │  ApoCommand = std::variant<      │
              │  (15 factories，顺序 │   │    monostate,                    │
              │   即 APO 的优先级)   │   │    PreampCmd, BiquadCmd,         │
              │  + parseLine(...)    │   │    IirFilterCmd, DelayCmd,       │
              └──────────────────────┘   │    CopyCmd, GraphicEqCmd,        │
              ┌──────────────────────┐   │    ConvolutionCmd, IncludeCmd,   │
              │     ApoEmitter       │   │    DeviceCmd, ChannelCmd,        │
              │  + render(key, cmd)  │   │    StageCmd, IfCmd, EvalCmd,     │
              └──────────┬───────────┘   │    VstPluginCmd,                 │
                         │                │    LoudnessCorrectionCmd>       │
              ┌──────────▼───────────┐   └──────────────────────────────────┘
              │        ApoNum        │
              │  QString text (权威) │   ┌──────────────────────────────────┐
              │  double  value       │   │          ApoTokens               │
              │  + parseFrequency()  │   │  FilterToken 18 项 + 参数表      │
              │  + make(v,dec,isFreq)│   │  gainRequired/qRequired/         │
              └──────────────────────┘   │  defaultQ/defaultS/usesCornerFreq│
                                         └──────────────────────────────────┘
              ┌──────────────────────┐   ┌──────────────────────────────────┐
              │      ApoChannels     │   │            ApoEnv                │
              │  9 标准名 + 别名解析 │   │  configPath()/installPath()/     │
              └──────────────────────┘   │  resolveRelative()               │
                                         └──────────────────────────────────┘
```

### 4.3 关键头文件签名

```cpp
// ===================== src/apocfg/ApoNum.h =====================
#pragma once
#include <QString>
#include <QStringView>

namespace dreamdsp::apocfg {

/// 一个数字：写在文件里的原文 + 解析出来的值。
/// text 对 round-trip 权威；value 对 DSP 权威。二者不可互相推导。
struct Num {
    QString text;        ///< 原样，如 "50,0" / "8.000" / "1e3"
    double  value = 0.0;

    /// wcstod 语义，C locale。allowComma 决定是否先把 ',' 换成 '.'
    /// （Filter/Preamp/Delay 无条件换；GraphicEQ 仅当整串无 '.' 时换；
    ///   Copy/IIR/Convolution/Eval/VSTPlugin 绝不换）
    static Num parseGeneric(QStringView s, bool allowComma);

    /// 频率专用。额外做两件事：
    ///  1. 剥掉 U+00A0（Fc 正则的字符类允许不换行空格）
    ///  2. 复刻 REW 千分位 hack：len>=5 且无 e/E 且 s[len-4]=='.' → value *= 1000
    ///     证据 BiQuadFilterFactory.cpp:220-241。"8.000" 读成 8000！
    static Num parseFrequency(QStringView s);

    /// 永远输出 '.'（QLocale::c()）。isFrequency 时断言 decimals != 3。
    static Num make(double v, int decimals, bool isFrequency = false);
};

/// 生成频率文本时的强制守卫：恰好 3 位小数会被 APO 乘以 1000。
[[nodiscard]] bool isFrequencyTextSafe(const QString& text);

} // namespace
```

```cpp
// ===================== src/apocfg/ApoTokens.h =====================
#pragma once
namespace dreamdsp::apocfg {

/// 保留"写在文件里的那个 token"，不要塌缩。PK 和 PEQ 的 DSP 完全相同，
/// 但必须分别 round-trip 回去。
enum class FilterToken {
    PK, PEQ, Modal,        // peaking
    LP, LPQ,               // low-pass
    HP, HPQ,               // high-pass
    BP,
    LS, LSC,               // low-shelf ：LS = corner f，LSC = center f
    HS, HSC,
    NO,                    // notch
    AP,                    // all-pass
    None,                  // 合法的 no-op 占位，不产生任何 filter，也不报错
    IIR,                   // 由 IirFilterCmd 处理
    Unknown
};

enum class BiquadType { Peaking, LowPass, HighPass, BandPass,
                        LowShelf, HighShelf, Notch, AllPass, None };

/// 形状参数写成了什么形式
enum class ShapeKind { Absent, Q, BandwidthOct, SlopeDb };

FilterToken  tokenFromString(QStringView s);
const char*  tokenToString(FilterToken t);
BiquadType   toType(FilterToken t);

/// token 末位不是 'C' → 用 corner frequency（BiQuadFilterFactory.cpp:196-197）
bool   usesCornerFreq(FilterToken t);
/// PK/PEQ/Modal/LS/LSC/HS/HSC 必须有 Gain，缺了整条 filter 被丢弃
bool   gainRequired(FilterToken t);
/// LP/LPQ/HP/HPQ/NO/AP 的 Gain 被静默忽略
bool   gainIgnored(FilterToken t);
/// PK/PEQ/Modal/AP 必须有 Q/BW，缺了（或值恰好为 0）是 ERROR
bool   qRequired(FilterToken t);
/// LP/HP/BP -> M_SQRT1_2 ; NO -> 30.0 ; 其余 -> NaN
double defaultQ(FilterToken t);
/// LS/LSC/HS/HSC -> 0.9
double defaultS(FilterToken t);

} // namespace
```

```cpp
// ===================== src/apocfg/ApoLine.h =====================
#pragma once
#include "apocfg/ApoCommands.h"
#include <QString>

namespace dreamdsp::apocfg {

enum class LineKind {
    Blank,     ///< 空行或纯空白
    Comment,   ///< trim 后以 '#' 开头 —— 仅为 UI 显示提示，语法上无意义
    Unknown,   ///< 没有 ':'，或 key 不匹配任何命令 → APO 直接忽略
    Command    ///< 已识别，cmd 有效
};

class ApoLine
{
public:
    // ---- round-trip 载荷（!dirty 时永远权威）----
    QString  raw;                 ///< 原样读入，已去掉 CR/LF
    LineKind kind = LineKind::Unknown;

    // ---- 解析 overlay ----
    QString  key;                 ///< 第一个 ':' 之前，已 trim
    QString  value;               ///< 第一个 ':' 之后，**绝不 trim**
    int      colonPos = -1;       ///< -1 表示整行没有 ':'
    ApoCommand cmd;

    // ---- 编辑标记 ----
    bool dirty = false;                ///< 只有 GUI 改过才置位
    bool hasInlineExpression = false;  ///< 有未转义的 '`' → 视为不透明，只读

    /// 只做三件事：indexOf(':') / left().trimmed() / mid(pos+1)。
    /// 完全复刻 FilterEngine.cpp:311-354。没有冒号的行**不做任何切分**。
    static ApoLine parse(const QString& rawLine);

    /// dirty ? ApoEmitter::render(key, cmd) : raw
    QString render() const;

    /// 把 "Filter" 后到 ':' 之间的原文取出来（"" / " 1" / "  12"），
    /// 让 `Filter  1:` 逐字符还原。BiQuadFilterFactory.cpp:72 是前缀匹配。
    QString filterIndexToken() const;
};

} // namespace
```

```cpp
// ===================== src/apocfg/ApoDocument.h =====================
#pragma once
#include "apocfg/ApoLine.h"
#include <QByteArray>
#include <QVector>

namespace dreamdsp::apocfg {

class ApoDocument
{
public:
    enum class Eol { Crlf, Lf };

    QString          filePath;
    QVector<ApoLine> lines;

    // 载入时捕获的字节级事实，写回时原样复现
    Eol  eol = Eol::Crlf;              ///< APO Editor 写 CRLF
    bool hadBom = false;               ///< 有 BOM = APO 读不了第一行，要警告
    bool wasAnsiFallback = false;      ///< UTF-8 解码出 U+FFFD → 退回 CP_ACP
    bool trailingNewline = false;      ///< APO Editor 用 join 不用 terminate

    /// 解码顺序完全同 FilterEngine.cpp:318-320：先 CP_UTF8，
    /// 结果含 U+FFFD 才退 CP_ACP。**不剥 BOM**（APO 也不剥），但记录 hadBom。
    /// 文件不存在不是错误，返回空文档。
    static ApoDocument load(const QString& path, QString* error = nullptr);

    QByteArray serialise() const;

    /// UTF-8 无 BOM，按 eol join，按 trailingNewline 决定是否补尾。
    /// 内部走 platform::AtomicWriter（temp + MoveFileEx），
    /// 让 APO 的 watcher 只看到一次完整替换。
    bool saveAtomic(QString* error = nullptr) const;

    /// 自检：没有任何 dirty 行时，serialise() 必须 == 载入时的原始字节。
    bool roundTripsExactly(const QByteArray& original) const;

    // ---- 语义查询（不修改文档）----
    int  indexOfCommand(QStringView key, int from = 0) const;
    bool hasInclude(QStringView fileName) const;

    // ---- 受控修改（唯一允许改文档的入口）----
    void setCommand(int lineIndex, const ApoCommand& c);  ///< 置 dirty
    void insertLine(int at, const QString& rawText);
    void removeLine(int at);
    void ensureInclude(const QString& fileName);          ///< 幂等
    void removeInclude(const QString& fileName);
};

} // namespace
```

### 4.4 round-trip 保真怎么做（六条硬规则）

| # | 规则 | 违反的后果 | 依据 |
|---|---|---|---|
| **R1** | 未被用户编辑的行，输出 `raw`，一个字节不改 | `configbeforePeace.txt:4` 的 `-1.8` 消失；用户手写的 `Convolution: church.wav` 被"规范化" | `Editor/MainWindow.cpp:286-333` |
| **R2** | `value` 在 parse 阶段**不 trim** | `Filter  1: ON  PK` 的多空格被压成单空格，diff 噪音爆炸 | `FilterEngine.cpp:322-323`，key 才 trim |
| **R3** | 每个数字保留 `text` 原文，编辑单个字段时**只重排该字段** | `0.50` → `0.5`、`50,0` → `50.0` 这类无意义改写 | `Num` 结构 |
| **R4** | 写入永远 UTF-8 **无 BOM** + CRLF；`trailingNewline` 按原样 | BOM 会让 U+FEFF 粘到第一个 key 上，第一条命令直接失效 | `FilterEngine.cpp:318-320` 无 BOM 处理 |
| **R5** | 含未转义反引号的行 → `hasInlineExpression = true`，GUI 里**只读**，永远原样回吐 | `iir_lowpass.txt:18` 那种 sample-rate 相关的系数被当成数字解析后毁掉 | `ExpressionFilterFactory.cpp:62-129` |
| **R6** | 频率输出**绝不使用恰好 3 位小数** | `Fc 8.000 Hz` 被 APO 读成 8000 Hz | `BiQuadFilterFactory.cpp:220-241` |

**保真的验证方式（这是唯一可信的验证，必须做成 CI）：**

```cpp
// tests/tst_apocfg_roundtrip.cpp 的骨架
void TestApoCfg::roundTripAllRealFiles_data() {
    QTest::addColumn<QString>("path");
    const QString cfg = dreamdsp::apocfg::env::configPath();   // 注册表读出来
    QDirIterator it(cfg, {"*.txt"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { const QString p = it.next(); QTest::newRow(qPrintable(p)) << p; }
}

void TestApoCfg::roundTripAllRealFiles() {
    QFETCH(QString, path);
    QFile f(path); QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray original = f.readAll();

    const auto doc = dreamdsp::apocfg::ApoDocument::load(path);
    QCOMPARE(doc.serialise(), original);      // 字节级，不是行级
}
```
数据集：本机 `D:\Program Files\EqualizerAPO\config` 下所有 `.txt`（含 `config.txt`、`configbeforePeace.txt`、`demo.txt`、`example.txt`、`multichannel.txt`、`iir_lowpass.txt`、`selective_delay.txt`，以及 4 个约 20 MB 的 AutoEQ 数据库——后者顺便当性能测试）。

**"编辑单行"的保真测试同样重要：**
```cpp
// 只改第 5 行的 Gain，其余所有行必须逐字节不变
auto doc = ApoDocument::load(p);
auto cmd = std::get<BiquadCmd>(doc.lines[4].cmd);
cmd.gainDb = Num::make(-3.5, 1);
doc.setCommand(4, cmd);
const QByteArray after = doc.serialise();
// 逐行比对：只有 index 4 允许不同
```

### 4.5 特别提醒：`Filter` 的前缀匹配

`BiQuadFilterFactory.cpp:72` 是 `command.find(L"Filter") == 0`，**不是** `==`。这意味着：
- `Filter:` / `Filter 1:` / `Filter  12:` / `FilterXYZ:` **全部**会被当成 filter 解析。
- **`# Filter: ...` 不会**（key 是 `# Filter`，不以 "Filter" 开头）。
- 但 **`Filter #1: ON PK ...` 会被解析**（key 以 "Filter" 开头）。

所以 `ApoEmitter` 里有一条硬约束：**除非你真的要发一条 filter，否则永远不要产生任何 trim 后以 `Filter` 开头的 key**。

其余 14 个命令全是**大小写敏感的精确 `==`**（`CopyFilterFactory.cpp:33`、`PreampFilterFactory.cpp:33`、`IncludeFilterFactory.cpp:53` 等）。

---

## 5. QML 界面设计

### 5.1 组件选型表

| 界面区域 | HuskarUI 组件 | 关键属性 / 陷阱 |
|---|---|---|
| 窗口壳 | `HusWindow` | 根元素。QWindowKit 全部内置（`HusWindow.qml:180 objectName:'__HusWindow__'` + `:203-205 HusWindowAgent`）。**绝不自己建 QWK agent，也绝不嵌套 HusWindow**（`docs/General/HusWindow.md:54-73` 明确警告） |
| 标题栏 | `captionBar`（`HusWindow` 内置 `HusCaptionBar`） | 放任何可点击控件进去，**必须** `captionBar.addInteractionItem(item)`，否则点击被拖拽区吃掉（`HusCaptionBar.qml:215-220`） |
| 左侧导航 | `HusMenu` | `initModel` + `defaultSelectedKeys` + `onClickMenu(deep,key,keyPath,data)`；折叠用 `compactMode: HusMenu.Mode_Compact` |
| 页面容器 | `StackLayout`（Qt 原生） | 不用 `HusTabView`——左导航 + StackLayout 更省，且 HusTabView 自带模型会跟我们的 model 打架 |
| 卡片 | 现有 `SectionCard.qml` | 已存在。也可换 `HusCard`，但 `HusCard` 的内容要塞进 `bodyDelegate: Component{}`，对 Layout 不友好，**保留自制 SectionCard** |
| EQ 滑块 | `HusSlider` | `orientation: Qt.Vertical`；**写 `value`、读 `currentValue`**；用 `onFirstMoved`/`onFirstReleased`，**不要双向 binding**；`radiusBg` 是 `HusRadius` 对象（`HusSlider.qml:69`），写 `radiusBg.all: 4` |
| 频率/增益/Q 精确输入 | `HusInputNumber` | `afterLabel` 可以给**数组** `['Hz','kHz']` + `onAfterActivated` 白送一个单位切换器；`precision` 控制小数位；`useWheel: true` |
| 滤波器类型 | `HusSegmented`（少）/ `HusSelect`（18 项全量） | `HusSegmented.options` 是 `{label,value,enabled,toolTip,iconSource}` 数组 |
| 设备下拉 | `HusSelect` | model 是 `{label,value,enabled}` 的 JS 数组；`onActivated(index)`；`clearEnabled` 默认 true，EQ 设备选择要 **`clearEnabled: false`** |
| 开关 | `HusSwitch` | `checkedText`/`uncheckedText`；`onToggled` |
| 滤波器精确表 | `HusTableView` | 注意文档级警告：`appendRow/setRow/removeRow` 作用于**当前排序+过滤后**的模型，不改 `initModel`，改完要 `filter()` |
| 分组标题 | `HusDivider { title: '...' }` | |
| 所有文字 | `HusText` | 不用 `Text`。`HusText` 已把 `renderType`/`color`/`font.family`/`font.pixelSize` 绑到主题（`src/imports/HusText.qml:27-38`） |
| Toast | `HusMessage` | 按官方 idiom `parent: root.captionBar` + `anchors.top: parent.bottom` + `z: 999` |
| 确认框 | `HusModal` | **`onConfirm`/`onCancel` 里必须自己 `close()`**，它不自动关 |
| 高级面板 | `HusDrawer` | `edge: Qt.RightEdge`，内容放 `contentDelegate: Component{}` |
| 悬浮说明 | `HusToolTip` | 直接嵌在被 hover 的控件里，`visible: parent.hovered`。**不要写 `parent: parent`** |
| 气泡 | `HusPopover` | **必须显式给 `width`**，否则塌成 0（`docs/Feedback/HusPopover.md`） |
| 托盘 | `Qt.labs.platform.SystemTrayIcon` | 需要 `QApplication` + `Qt6::Widgets` |

### 5.2 主界面 QML 骨架（可运行）

```qml
// qml/Main.qml
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Qt.labs.platform as Platform
import HuskarUI.Basic
import DreamDSP

import "components"
import "pages"

HusWindow {
    id: win

    width: 1180
    height: 760
    minimumWidth: 940
    minimumHeight: 600
    visible: true
    title: 'DreamDSP'
    followThemeSwitch: true

    // ---------------------------------------------------------- 标题栏
    captionBar.height: 36
    captionBar.winTitle: 'DreamDSP'
    captionBar.showWinIcon: false
    captionBar.showThemeButton: true
    captionBar.showTopButton: true
    captionBar.themeCallback: () => {
        HusTheme.darkMode = HusTheme.isDark ? HusTheme.Light : HusTheme.Dark
        win.setWindowMode(HusTheme.isDark)
    }
    captionBar.topCallback: (checked) => HusApi.setWindowStaysOnTopHint(win, checked)

    Component.onCompleted: {
        HusTheme.darkMode = HusTheme.Dark
        HusTheme.installThemePrimaryColorBase('#3d7eff')
        HusTheme.installThemePrimaryRadiusBase(8)
        HusTheme.textRenderType = HusTheme.NativeRendering
        win.setWindowMode(true)
        // 返回 false 说明系统不支持 → 回落到纯色背景（会恢复 colorBgBase）
        if (!win.setSpecialEffect(HusWindow.Win_Mica))
            win.setSpecialEffect(HusWindow.None)
    }

    onClosing: (close) => {
        AppController.flushNow()
        if (SettingsStore.closeToTray) { close.accepted = false; win.hide() }
    }

    // HusWindow 不画自己的背景（Mica 模式下 window.color 被设成 transparent），
    // 非 Mica 时必须自己铺一层，否则整窗透明 = 白底 + 看不清的浅色文字。
    Rectangle {
        anchors.fill: parent
        z: -1
        color: win.specialEffect === HusWindow.None
               ? HusTheme.Primary.colorBgBase
               : 'transparent'
        Behavior on color { ColorAnimation { duration: Theme.durMid } }
    }

    // ------------------------------------------------------------ Toast
    HusMessage {
        id: toast
        z: 999
        parent: win.captionBar
        width: parent.width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.bottom
    }

    Connections {
        target: AppController
        function onWriteFailed(reason) { toast.error('写入失败：' + reason) }
        function onExternalConfigChanged() { toast.warning('config.txt 被外部程序修改') }
    }

    // ------------------------------------------------------------- 托盘
    Platform.SystemTrayIcon {
        visible: true
        icon.source: 'qrc:/qt/qml/DreamDSP/qml/assets/tray.png'
        tooltip: 'DreamDSP'
        menu: Platform.Menu {
            Platform.MenuItem {
                text: AppController.eqEnabled ? '停用均衡' : '启用均衡'
                onTriggered: AppController.eqEnabled = !AppController.eqEnabled
            }
            Platform.MenuSeparator {}
            Platform.MenuItem { text: '显示主窗口'; onTriggered: { win.show(); win.raise() } }
            Platform.MenuItem { text: '退出'; onTriggered: { AppController.flushNow(); Qt.quit() } }
        }
        onActivated: (reason) => {
            if (reason === Platform.SystemTrayIcon.Trigger) { win.show(); win.raise() }
        }
    }

    // ------------------------------------------------------------ 主体
    RowLayout {
        anchors.top: win.captionBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0

        // ---- 左侧导航 ----
        HusMenu {
            id: nav
            Layout.fillHeight: true
            Layout.preferredWidth: 208
            defaultMenuWidth: 208
            defaultSelectedKeys: ['eq']
            compactMode: HusMenu.Mode_Standard
            initModel: [
                { key: 'eq',      label: '均衡器',   iconSource: HusIcon.SlidersOutlined },
                { key: 'filters', label: '滤波器',   iconSource: HusIcon.LineChartOutlined },
                { key: 'presets', label: '预设',     iconSource: HusIcon.FolderOpenOutlined },
                { key: 'routing', label: '声道路由', iconSource: HusIcon.SoundOutlined },
                { key: 'raw',     label: '配置文本', iconSource: HusIcon.ThunderboltOutlined },
                { key: 'setting', label: '设置',     iconSource: HusIcon.SettingOutlined }
            ]
            onClickMenu: (deep, key) => {
                const order = ['eq', 'filters', 'presets', 'routing', 'raw', 'setting']
                const i = order.indexOf(key)
                if (i >= 0) stack.currentIndex = i
            }
        }

        HusDivider { Layout.fillHeight: true; orientation: Qt.Vertical }

        // ---- 右侧内容 ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 12

            // 顶部工具条：全局开关 / 接管状态 / 设备
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                HusSwitch {
                    checked: AppController.eqEnabled
                    checkedText: 'ON'
                    uncheckedText: 'OFF'
                    onToggled: AppController.eqEnabled = checked
                }

                StatusPill {
                    text: AppController.engaged ? '已接管 config.txt' : '未接管'
                    showDot: true
                    dotColor: AppController.engaged ? HusTheme.Primary.colorSuccess
                                                    : HusTheme.Primary.colorTextQuaternary
                }

                Item { Layout.fillWidth: true }

                HusText { text: '输出设备'; font.pixelSize: 12; opacity: 0.6 }

                HusSelect {
                    Layout.preferredWidth: 280
                    sizeHint: 'small'
                    clearEnabled: false
                    model: AppController.devices.selectModel()
                    currentIndex: AppController.currentDevice
                    onActivated: (i) => AppController.currentDevice = i
                }

                HusIconButton {
                    iconSource: HusIcon.ReloadOutlined
                    iconSize: 14
                    sizeHint: 'small'
                    onClicked: AppController.refreshDevices()
                    HusToolTip {
                        visible: parent.hovered
                        showArrow: true
                        position: HusToolTip.Position_Bottom
                        text: '重新枚举音频设备'
                    }
                }

                HusButton {
                    text: AppController.engaged ? '停止接管' : '接管'
                    type: AppController.engaged ? HusButton.Type_Default : HusButton.Type_Primary
                    enabled: AppController.apoFound && AppController.configWritable
                    onClicked: AppController.engaged = !AppController.engaged
                }
            }

            // 页面栈
            StackLayout {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0

                EqualizerPage {}
                FiltersPage   {}
                PresetsPage   {}
                RoutingPage   {}
                RawConfigPage {}
                SettingsPage  {}
            }

            // 页脚
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                StatusPill {
                    text: AppController.apoFound
                          ? 'Equalizer APO ' + AppController.apoVersion
                          : '未检测到 Equalizer APO'
                    dotColor: AppController.apoFound ? HusTheme.Primary.colorSuccess
                                                     : HusTheme.Primary.colorError
                }

                HusText {
                    Layout.fillWidth: true
                    text: AppController.lastError !== '' ? AppController.lastError
                                                         : AppController.apoConfigPath
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                    opacity: AppController.lastError !== '' ? 0.95 : 0.45
                    color: AppController.lastError !== '' ? HusTheme.Primary.colorError
                                                          : HusTheme.Primary.colorTextBase
                }

                HusIconButton {
                    text: '配置目录'
                    iconSource: HusIcon.FolderOpenOutlined
                    iconSize: 14
                    sizeHint: 'small'
                    onClicked: AppController.openConfigFolder()
                }
            }
        }
    }
}
```

### 5.3 `qml/components/BandStrip.qml`（修正版，可运行）

```qml
// qml/components/BandStrip.qml
import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic

Item {
    id: strip

    property int    bandIndex: 0
    property real   gain: 0.0
    property string label: ''
    property real   range: 15.0

    signal gainEdited(real value)

    implicitWidth: 46

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // 数值读数：非 0 时才高亮，避免一排 "0.0" 的视觉噪音
        HusText {
            Layout.alignment: Qt.AlignHCenter
            text: (strip.gain > 0.05 ? '+' : '') + strip.gain.toFixed(1)
            font.pixelSize: 11
            opacity: Math.abs(strip.gain) < 0.05 ? 0.35 : 1.0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }

        HusSlider {
            id: sl
            Layout.alignment: Qt.AlignHCenter
            Layout.fillHeight: true
            orientation: Qt.Vertical
            min: -strip.range
            max:  strip.range
            stepSize: 0.1
            snapMode: HusSlider.SnapAlways
            radiusBg.all: 3                       // radiusBg 是 HusRadius 对象

            // 单向：C++ -> QML。写回只走信号，绝不在 onValueChanged 里赋值，
            // 否则 binding 会被命令式赋值打断。
            value: strip.gain
            onFirstMoved:    strip.gainEdited(sl.currentValue)
            onFirstReleased: strip.gainEdited(sl.currentValue)

            HusToolTip {
                visible: sl.pressed
                showArrow: true
                position: HusToolTip.Position_Right
                text: sl.currentValue.toFixed(1) + ' dB'
            }
        }

        HusText {
            Layout.alignment: Qt.AlignHCenter
            text: strip.label
            font.pixelSize: 10
            opacity: 0.55
        }
    }

    // 双击归零
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onDoubleClicked: strip.gainEdited(0.0)
    }
}
```

### 5.4 现代化的四个具体手段

1. **深色 + 强调色**：`HusTheme.darkMode = HusTheme.Dark` + `installThemePrimaryColorBase('#3d7eff')`（一次调用重生成完整 1..10 色阶）。设置页给一排预设色让用户点。
2. **圆角卡片**：`installThemePrimaryRadiusBase(8)`；卡片自己用 `radius: Theme.radiusLg`、`border.color: HusTheme.Primary.colorBorderSecondary`、`color: HusTheme.Primary.colorBgContainer`。
3. **平滑动画**：所有颜色变化挂 `Behavior on color { enabled: HusTheme.animationEnabled; ColorAnimation { duration: Theme.durMid } }`。全局关动画只需 `HusTheme.animationEnabled = false`（几乎每个组件的 `animationEnabled` 都默认取它）。
4. **无边框**：`HusWindow` 已内置。加 `setSpecialEffect(HusWindow.Win_Mica)`，失败静默回落。

---

## 6. 风险与已知坑

### 6.1 HuskarUI

| # | 坑 | 后果 | 对策 |
|---|---|---|---|
| H1 | **Qt SDK 里的 0.5.2.0 会抢**（`D:\Qt\6.8.3\msvc2022_64\lib\cmake\HuskarUI\HuskarUIConfigVersion.cmake:13`，且 `qml/HuskarUI/` 下只有 `Basic`、没有 `Impl`） | `HusWindow.qml:25 import HuskarUI.Impl` 运行时 QML 报 module not installed | ① CMake：`find_package(HuskarUI 0.7 REQUIRED NO_DEFAULT_PATH PATHS ${HuskarUI_DIR})` + 显式版本断言；② main.cpp：**prepend** import path（现有代码已对），因为 `addImportPath()` 是 append，压不住排在前面的 SDK 路径 |
| H2 | `qmldir` 的 `linktarget huskaruibasicplugin` 与导出的 `HuskarUI::huskaruibasicplugin` 不匹配 | 每次 configure 一条 CMake warning："The plugin will not be linked" | 动态构建下**无害**（插件运行时加载）。**唯一需要动作的场景**：切 `BUILD_HUSKARUI_STATIC_LIBRARY=ON` 时必须加 `Q_IMPORT_QML_PLUGIN(HuskarUI_BasicPlugin)` + `Q_IMPORT_QML_PLUGIN(HuskarUI_ImplPlugin)` 并链两个 plugin target。**决定：坚持 shared，不切 static** |
| H3 | `HusSlider.value` 是 `property var`，`currentValue` 才是 readonly 的解析结果（`HusSlider.qml:48-49`）；且**没有 `moved()` 信号**，只有 `firstMoved()`/`firstReleased()` | 用 `from`/`to`/`onMoved` 写法静默无效 | 见 §5.3 |
| H4 | 多数组件的 `radiusBg` 是 **`HusRadius` 对象**不是数字（`src/cpp/items/husradius.h:11-52`），包括 `HusSlider`（`HusSlider.qml:69` 已复核） | `radiusBg: 8` 类型不匹配、静默失效 | 写 `radiusBg.all: 8` 或 `radiusBg { topLeft: 8; topRight: 8 }` |
| H5 | 主题 token **字体尺寸/圆角是字符串**（库自己 `HusText.qml:36` 做 `parseInt`） | `radius: HusTheme.Primary.radiusPrimaryLG` 得到 NaN | 集中到 `qml/Theme.qml` singleton |
| H6 | 标题栏里的可点击控件被拖拽区吃掉（`HusCaptionBar.qml:215-220`） | 点击无响应，怎么调都没反应 | `captionBar.addInteractionItem(item)`，抄 `gallery/qml/Gallery.qml:44-51` 在 `onWindowAgentChanged` 里注册 |
| H7 | `HusModal` 的 `onConfirm`/`onCancel` **不自动 close** | 对话框关不掉 | 自己调 `close()` |
| H8 | `HusPopover` 不给 `width` 就塌成 0 | 气泡不可见 | 显式设 `width` |
| H9 | `HusTableView` 的 `appendRow/setRow/removeRow` 作用于**排序过滤后**的模型，不改 `initModel` | 改了数据但源模型没变，刷新后回滚 | 改完调 `filter()`；或干脆只用它做只读展示，编辑走 `HusInputNumber` |
| H10 | **windeployqt 不拷 `HuskarUIBasic.dll`**（只拷 `huskaruibasicplugin.dll`，而后者直接 import 前者） | 部署包启动即 `objectCreationFailed` → 退出码 -1 | `scripts\deploy.bat` 里手工 `copy` 两个 DLL。HuskarUI 自己的 gallery 也是这么干的（`gallery/CMakeLists.txt:259-260`）。**不要传 `--no-widgets`**（托盘需要 Qt6Widgets.dll） |
| H11 | **待验证**：`setSpecialEffect(Win_Mica)` 在 Win11 26200 上的实际行为没人跑过 | 可能整窗透明看不清 | 必须检查返回值并回落到 `HusWindow.None` |

### 6.2 QML 性能

| # | 坑 | 对策 |
|---|---|---|
| P1 | 每帧 emit `dataChanged` → 全链路重算 | `EqBandModel::setGain` 里**先 `qFuzzyCompare` 早退**再 emit（现有代码已有类似逻辑，确认保留） |
| P2 | `QQuickPaintedItem::paint()` 跑在**渲染线程**，不是 GUI 线程 | 里面绝不碰 model 指针的可变状态。做法：model 变化时在 GUI 线程把 31 段参数**快照**成一份 POD 数组存进 item，`paint()` 只读快照 |
| P3 | 拖动时 60 Hz 触发重算 | `update()` 只是"调度"一次重绘，同帧内 N 次调用会自动合并成一次 `paint()`。**不要手写节流** |
| P4 | 曲线用 `Shape`/`ShapePath` | 官方文档明说改 path element 会**每次变化都重新三角化**。**别用**。用 `QQuickPaintedItem`（Qt 6.8 上 render target 恒为 QImage，`setRenderTarget` 是 no-op） |
| P5 | `HusTheme.animationEnabled` 是全局动画开关 | 设置页给一个"减少动效"开关直接改它 |
| P6 | 未测量的性能数字 | **待验证**：31 段 × 256 探点 ≈ 8k 次 biquad 幅频计算/帧，以及 900×240 @ DPR 1.5 ≈ 1.9 MB 纹理上传/重绘——**这两个数字是算出来的、没有实测**。M1 验收时必须实测拖动帧时；只有实测超预算才考虑迁到 `QSGGeometryNode` |

### 6.3 COM / 线程

| # | 坑 | 对策 |
|---|---|---|
| C1 | `IMMNotificationClient` 回调在 **MMDevAPI/RPC 线程**，且 `LPCWSTR` 参数**只在调用期间有效** | 通知对象做成纯 `IUnknown`（**不是 QObject**），回调里先 `QString::fromWCharArray` 拷贝，再 `QMetaObject::invokeMethod(bridge, lambda, Qt::QueuedConnection)` |
| C2 | 回调里 `Release()` enumerator | 死锁/崩溃 | 关闭顺序固定：先 `UnregisterEndpointNotificationCallback` → 再 `detach()`（让在途 post 变 no-op）→ 最后 `Release()` |
| C3 | `RegisterHotKey` **线程亲和** | `WM_HOTKEY` 投到调用线程的消息队列 | 必须在 Qt 事件循环线程注册，`UnregisterHotKey` 也在同线程。**已实证**：`RegisterHotKey(nullptr, ...)` 产生的 `hwnd==NULL` 的 `WM_HOTKEY` **确实**会被 `QAbstractNativeEventFilter` 收到（eventType `"windows_generic_MSG"`）——不需要自建窗口 |
| C4 | Qt 6 的 filter 签名是 `qintptr* result` 不是 `long*` | 编译错误 | `D:/Qt/6.8.3/msvc2022_64/include/QtCore/qabstractnativeeventfilter.h:19` |
| C5 | `MOD_NOREPEAT` **不会**在 `LOWORD(lParam)` 里回显 | 别拿 lParam 反查修饰键做校验 | |
| C6 | `Q_PROPERTY(SomeClass* x ...)` 要求 **moc 编译期完整类型** | 前向声明能编过 `.cpp`，但 `mocs_compilation.cpp` 里 `static_assert` 炸（C2338，报错位置和你改的文件毫无关系） | 在头文件里 `#include` 该类型的头，不用前向声明。现有 `AppController.h` 已 `#include "app/EqBandModel.h"`，**保持** |
| C7 | `PKEY_Device_FriendlyName` 对已拔掉的 devnode 返回 `0xE000020B` | 用户最想配的离线设备名字空白 | 4 级回退：FriendlyName → DeviceDesc + " (" + IfFriendlyName + ")" → DeviceDesc → 注册表 `{a45c254e-...},2` |
| C8 | 注册表必须走 **64 位视图** | 本机 APO 装在 D:，且 **没有 WOW6432Node 镜像**，32 位视图什么也读不到 | `RegOpenKeyExW(..., KEY_READ \| KEY_WOW64_64KEY, ...)` |
| C9 | `DEVICE_STATE_DISABLED` 在注册表里**不是 0x2**，是 `0x10000001` | 禁用设备被误判为启用 | 用 `IMMDevice::GetState()`（已归一化）；若必须读注册表，`(raw & 0x0000000F)` 且把 `raw & 0x10000000` 当禁用 |
| C10 | Peace 的 APO 检测有**大小写 bug**（`Peace.au3:28047` 左侧 `StringLower`、右侧 `$EqualizerAPOPost` 是大写常量没包 `StringLower`） | 只装了 post-mix 的设备被判为"未安装" | **别照抄**。用 `CLSIDFromString` + `IsEqualGUID` 做 GUID 级比较 |

### 6.4 APO 写文件竞态

| # | 坑 | 对策 |
|---|---|---|
| A1 | APO 用 `GENERIC_READ / FILE_SHARE_READ`（**不共享写**）打开配置，并 `Sleep(1)` 死等 | 它持有文件期间，**我们的** `MoveFileEx`/`CreateFile(GENERIC_WRITE)` 会 `ERROR_SHARING_VIOLATION` | 重试循环：`ERROR_SHARING_VIOLATION`/`ERROR_LOCK_VIOLATION`/`ERROR_ACCESS_DENIED` 视为可重试，前 10 次 2 ms、之后 20 ms，上限 ~1 s |
| A2 | 原地写会被 APO 读到半截 | 音频瞬间乱掉 | **绝不原地写**。写 temp → `FlushFileBuffers` → `CloseHandle` → `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING\|MOVEFILE_WRITE_THROUGH)`。rename 是原子的 |
| A3 | `MOVEFILE_COPY_ALLOWED` 跨卷时会退化为非原子的 copy+delete | 失去原子性且无声 | **不传这个 flag**。用 `GetVolumePathNameW` 校验同卷 |
| A4 | temp 文件放在 config 目录里会**触发两次 reload**（APO 的 watcher 是 `bWatchSubtree=TRUE` + `FILE_NOTIFY_CHANGE_FILE_NAME`） | 每次保存音频抖两下 | temp 放 `<InstallPath>\.dreamdsp-tmp\`（同卷、在被监视目录外）。**已实证非提权可写** |
| A5 | APO 的合并窗口只有 **10 ms** | 高频写会真的高频 reload | GUI 侧 150 ms trailing-edge debounce：**成员 `QTimer` + `setSingleShot(true)` + 每次变化 `start()`**（`start()` 对运行中的 timer 是 stop+restart，这就是合并机制）。**不要用静态的 `QTimer::singleShot`**——它无法重启也无法取消，60 Hz 拖动会排队 60 次写 |
| A6 | `QFileSystemWatcher` **一旦被监视文件被 rename/delete 就永久失效**——而这正是原子写、APO 官方 Editor 和 Peace 都在做的事 | DreamDSP 只能感知到第一次外部变更，之后彻底聋掉 | 同时监视**文件和它的父目录**；任何信号触发后 ~120 ms 防抖，再检查 `files().contains(path)`，缺了就 `addPath()` 重挂。父目录的 watch 是唯一能扛过 delete/recreate 的 |
| A7 | **Peace 还装在同一个目录**，它也写 `peace.txt` | 两个进程抢文件 | DreamDSP **只写 `dreamdsp.txt`**，`config.txt` 只做最小 read-modify-write。**永远不碰 `peace.txt`** |
| A8 | 每次编辑 `config.txt` 前不重读 | 覆盖掉 Peace 或官方 Editor 刚加的行 | `setEngaged()` 里每次都 `readLines` → 改 → `writeLines`。现有实现已如此（`AppController.cpp:84-107`），**保持** |
| A9 | 提权 | 不需要 | 本机 config 目录有**继承的** `Authenticated Users:(I)(M)`（注意：**不是**常说的 `BUILTIN\Users` 完全控制，`Users` 只有 RX）。以 `asInvoker` 发布，**不加 UAC manifest**。但因为是继承 ACE，启动时要**探测**：`CreateFileW(cfg+"\\.dreamdsp-probe", GENERIC_WRITE, 0, ..., CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY\|FILE_FLAG_DELETE_ON_CLOSE, ...)`，`ERROR_ACCESS_DENIED` 时给明确提示而不是每次保存静默失败 |

### 6.5 Peace 互操作（M3 以后，但设计要提前避坑）

| # | 坑 | 对策 |
|---|---|---|
| K1 | **`QSettings(IniFormat)` 会毁掉 `peace.ini`** | 实证：`Save Window Positions` → `Save%20Window%20Positions`（190 个键里 184 个被 %-编码）；`[Changing Sliders]` → `[Changing%20Sliders]`；`Beep On/Off=0` 里的 `/` 被当成 group 分隔符，凭空生出 `[Show]`/`[On]`/`[Beep%20On]`/`[%General]` 四个幽灵 section；文件从 197 行涨到 211 行 | **手写 `PeaceIni`**：按首个 `'='` 切分，保留原始行文本和顺序，只替换值。`QSettings` 只用于 DreamDSP 自己的私有配置（键名自己控制，不含空格和斜杠） |
| K2 | `QSettings` 给含 `'='` 的值**加引号** | `3 Way Crossover.peace` 的 `Routing=R=1.000*L C=1.000*L ...` 变成带引号的字符串 | 同上。**这条值是最好的单测金丝雀** |
| K3 | `.peace` 的 section 后缀规则：speaker 0 **无后缀** | 解析错位 | 正则 `^(Frequencies\|Gains\|Qualities\|Filters\|Disabled)(\d*)$`，空后缀 = 0 |
| K4 | `[Commands]` 的 ini key 编号与内部数组序**不一致**：key `"1"` 是 *commands after*，key `"2"` 是 *commands after device* | 生成的 APO 命令顺序静默错乱 | 照抄 `Peace.au3:26699-26701` 的映射 |
| K5 | `[Sliders]` 的键分隔符在版本间**变过**：老预设是 `Slider1-0`，当前源码读写 `Slider1 0`（空格） | 读不到标签 | 读时两种都接受：`^Slider(\d+)[- ](\d)$`；写用空格 |
| K6 | 重复 `[General]` section（`Equalizer One Third Octave.peace:1-12` 真有两个） | Win32 `GetPrivateProfileString` 是**首个匹配 section 胜出**，naive 合并解析器行为不同 | 首个 section 胜出；**自己写文件时永远不产生重复 section** |
| K7 | **待验证**：非 ASCII 编码。本机所有 `.peace`/`peace.ini` 都是纯 ASCII，但 `peace.ini:2 Language=Chinese simplified`——中文预设描述/设备名迟早出现。AutoIt 的 `IniWrite` → `WritePrivateProfileStringW`，只有文件已有 UTF-16 BOM 时才写 UTF-16，否则按系统 ANSI（本机大概率 GBK/CP936） | 读时嗅探 UTF-16 BOM，否则按系统 ANSI 解码；**这条必须实测后才能定稿** |

---

## 7. 接下来 3 步（可执行）

### 第 1 步 — 把地基钉死（约半天）

**目标：一条命令从干净树构建出可运行的 exe，且工具链路径与本机真实情况一致。**

1. 核对并修 `I:\Qt\DreamDSP\scripts\env.bat` 的 `VCVARS`：当前写死 `D:\Program Files\VisualStudio\2022\...`，但另一条证据显示本机 MSVC 是 `D:\Program Files\VisualStudio\2026\IDE\VC\Tools\MSVC\14.51.36231`。跑 `dir "D:\Program Files\VisualStudio"` 确认，改成实际存在的那个。
2. 用 §2.2 的完整内容**替换** `I:\Qt\DreamDSP\CMakeLists.txt`：
   - 加 `find_package(HuskarUI 0.7 REQUIRED NO_DEFAULT_PATH PATHS ${HuskarUI_DIR})` + 版本 `FATAL_ERROR` 断言；
   - 加 `Qt6::Widgets`；
   - 去掉 `RESOURCE_PREFIX "/"`；
   - 拆出 `dreamdsp_core` / `dreamdsp_apocfg` / `dreamdsp_peacefmt` / `dreamdsp_platform` 四个 static lib（此时 apocfg/peacefmt/platform 里先只放**已存在**的文件，新增的 .h/.cpp 建空壳）；
   - `CMAKE_CXX_STANDARD` 17 → 20。
3. 按 §2.3 改 `src/main.cpp`：`QApplication` + `setQuitOnLastWindowClosed(false)` + `loadFromModule("DreamDSP","Main")` + `aboutToQuit` 里 flush。
4. 建 `tests/CMakeLists.txt` + `tests/tst_apocfg_roundtrip.cpp`（先只测 `ApoConfig::readLines/writeLines` 对 `D:\Program Files\EqualizerAPO\config\*.txt` 的字节级 round-trip）。
5. **验收**：`scripts\build.bat clean` 从零走通；`ctest` 全绿；双击 exe 出深色无边框窗口。

### 第 2 步 — 收掉 M1 的 delta（约 1 天）

**目标：达到 §3.4 的 7 条验收标准。**

1. `src/platform/AtomicWriter.{h,cpp}`：temp（`<InstallPath>\.dreamdsp-tmp\`）→ `FlushFileBuffers` → `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)` + 分级退避重试。用 `GetVolumePathNameW` 校验同卷，不同卷时降级到 config 目录内并接受双 reload。
2. `src/platform/SafeFileWatcher.{h,cpp}`：文件 + 父目录双监视、120 ms 防抖、缺失时重挂载、`beginSelfWrite()/endSelfWrite()` 吞掉自写通知。
3. 改 `AppController`：
   - 构造函数里的无条件 `writeNow()` → 仅当 `dreamdsp.txt` 不存在时创建空文件；
   - `writeNow()` 改走 `AtomicWriter`；
   - 接 `SafeFileWatcher` 监视 `config.txt`，外部变更时刷新 `engaged` 并 emit `externalConfigChanged()`；
   - `Device:` 行改用 GUID 形式或"全部设备时不写 Device 行"，并对含 `;` 的设备名做降级。
4. 改 QML：
   - `qml/Theme.qml`（新增 singleton，见 §3.3-④）；
   - `qml/components/BandStrip.qml` 换成 §5.3 的版本；
   - `qml/Main.qml` 里 preamp 滑块改成 `onFirstMoved`/`onFirstReleased` + `currentValue`；
   - 修 `HusToolTip` 的 `parent: parent`；
   - `Component.onCompleted` 里加带返回值检查的 `setSpecialEffect(Win_Mica)`；
   - 加 `HusMessage` toast 并接 `AppController.writeFailed` / `externalConfigChanged`。
5. **实测拖动帧时**：在 `ResponseCurveItem::paint()` 里挂一个 `QElapsedTimer`，拖 10 根滑块 10 秒，打印 p50/p99。超过 8 ms 才考虑优化路径（§6.2-P6）。
6. **验收**：手动跑一遍 §3.4 的 7 条，特别是第 6 条 —— 接管/取消接管后 `config.txt` 必须能 `fc /b` 回到原始 20 字节。

### 第 3 步 — 起 libapocfg，用真实文件把 round-trip 钉死（约 2 天）

**目标：`ApoDocument` 能字节级 round-trip 本机每一个 APO 配置文件，`ApoParser` 能正确分类每一行。**

顺序（每一步都先写测试再写实现）：

1. `ApoNum` + `ApoTokens`：先把 **3 位小数 Fc 陷阱**（`"8.000"` → 8000）和 **逗号小数点的 per-command 规则**写成单测。这两条是整个 libapocfg 里最容易被"想当然"写错的。
2. `ApoLine::parse()`：只做 `indexOf(':')` / `left().trimmed()` / `mid(pos+1)`。测试用例必须包含：
   - `configbeforePeace.txt:4` 的裸 `-1.8`（无冒号）；
   - `configbeforePeace.txt:1` 的 `# Preamp: 0.4 dB`（有冒号，key = `# Preamp`，不匹配任何命令）；
   - `multichannel.txt:1` 的 `#Common preamp`（无冒号）；
   - `demo.txt:11` 的 `Fc     100H z`（Hz 被空格劈开）；
   - `demo.txt:10` 的 `Fc    50,0 Hz  Gain -10,0 dB  Q  2,50`（逗号小数点）；
   - `demo.txt:12` 的 `Fc   8.000 Hz`（千分位 hack，必须读成 8000）；
   - `iir_lowpass.txt:18` 的反引号表达式系数（必须标 `hasInlineExpression` 并只读）；
   - `selective_delay.txt:2` 的 `Copy: L2=L R2=R`（虚拟通道）。
3. `ApoDocument::load/serialise` + `roundTripsExactly`：对 `D:\Program Files\EqualizerAPO\config` 下**所有** `.txt` 做数据驱动测试。这一步跑绿之前不写任何 emitter。
4. `ApoParser` 的 15 个 factory，**严格按 APO 的顺序**（Device, If, Expression, Include, Stage, Channel, IIR, BiQuad, Preamp, Delay, Copy, Convolution, GraphicEQ, VSTPlugin, LoudnessCorrection）。注意 `Filter` 是**前缀匹配**、其余是**大小写敏感精确匹配**。
5. `ApoEmitter`：只在 `dirty` 时调用。写"改单行、其余逐字节不变"的测试。
6. **同时并行**：`svn export svn://svn.code.sf.net/p/equalizerapo/code/trunk` 拿 1.4.1 真源码，和 GitHub mirror（trunk r100，1.4.0 时代）diff 一遍。当前所有正则和格式串都是靠 `strings -el` 比对安装的 `EqualizerAPO.dll` 1.4.1.0 确认字节一致的，置信度高但**不是源码 diff**。在冻结 libapocfg 之前把这个补上。

---

## 附：明确标记为"待验证"的清单

1. **`scripts\env.bat` 的 VS 路径**（写的是 2022，另一处证据是 2026）。
2. **HuskarUI 的 MSVC 端到端构建**：`build\dreamdsp\DreamDSP.exe` 存在说明构建过，但我没有验证它是用 MSVC 还是被 PATH 上的 w64devkit GNU g++ 15.2.0 意外命中的。**第 1 步必须从 vcvars64 环境重跑一次 clean build 确认。**
3. **`setSpecialEffect(HusWindow.Win_Mica)` 在 Win11 26200 的运行时行为**（只读过代码，没跑过）。
4. **`ResponseCurveItem` 的实测帧时**（8k 次 biquad/帧、1.9 MB 纹理上传 —— 都是算出来的，没测）。
5. **托盘图标是否真的出现在通知区**（QML 加载无错只证明 `SystemTrayIcon` 实例化成功）。
6. **`HusWindow` + 关闭到托盘的共存**（QWindowKit frameless + hide/show 流程未测）。
7. **`.peace` / `peace.ini` 的非 ASCII 编码行为**（本机全 ASCII，但 `Language=Chinese simplified`）。
8. **`ReplaceFileW` vs `MoveFileEx`**：`MoveFileEx` 已实测非提权可用；`ReplaceFileW` 会额外保留目标的 ACL/属性/流，但未做 P/Invoke 直测。
9. **APO 1.4.1 真源码 diff**（当前结论基于 GitHub mirror + DLL 字符串比对）。
10. **Peace 的 `Effects()`（`Peace.au3:29375-29707`，~330 行）和 `SurroundEffect()`（`:29245-29373`）的完整 APO 文本输出** —— 只读了前 ~25 行。要做到"用了效果的预设也能字节匹配 Peace 的 peace.txt"，必须逐行移植这两个函数。这是 M3 的主要工作量，不要低估。
11. **`Peace.exe` 是否容忍键顺序被打乱** —— `IniRead` 设计上与顺序无关，但没实测。手写的 `PeaceIni` 保留行序，从根上绕开这个问题。
12. **`.peaceset` 格式**（本机没有样本，schema 未知）、**Theme\theme.ini / graphtheme.ini schema**、**AutoEQ 数据库格式**（约 20 MB，未检查）。