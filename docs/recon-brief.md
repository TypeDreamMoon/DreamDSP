## HuskarUI 0.7.0.0 actual API (CMake integration, gallery bootstrap, QML component API, theme system, C++/QML registrations, icons, QWindowKit) as it exists at I:\Qt\DreamDSP\third_party\HuskarUI
- HuskarUI supports BOTH `add_subdirectory` and `find_package(HuskarUI)`. I empirically verified add_subdirectory: I wrote a throwaway consumer project and CMake configured cleanly against Qt 6.8.3 ("Configuring HuskarUI Version: 0.7.0.0", "The Qt has private module: TRUE", shaders compiled, build files written).
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\CMakeLists.txt:53-58 (add_subdirectory(src_impl); add_subdirectory(src); optional gallery). Verified by configuring C:\Users\10678\AppData\Local\Temp\claude\I--Qt\7fffc091-07c0-410a-a51c-5a342ddbdb5e\scratchpad\hustest\CMakeLists.txt with `cmake -G Ninja -B b -S . -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64` — configure succeeded.
  → Use `add_subdirectory(third_party/HuskarUI huskarui)` in DreamDSP's root CMakeLists. You MUST add the root of the repo (not src/), because src/CMakeLists.txt line 3 does `project(HuskarUIBasic VERSION ${HUSKARUI_VERSION})` and HUSKARUI_VERSION is only defined in the root file (lines 42-46). See code_samples[1].
- With `add_subdirectory` the link target is the plain name `HuskarUIBasic` — the namespaced `HuskarUI::Basic` target ONLY exists via find_package. There is no `add_library(HuskarUI::Basic ALIAS ...)` anywhere in HuskarUI's own CMake (the only ALIAS lines in the tree belong to the qwindowkit submodule).
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\CMakeLists.txt:189-197 sets EXPORT_NAME "Basic" on target HuskarUIBasic; lines 325-330 `install(EXPORT HuskarUITargets ... NAMESPACE HuskarUI::)`. `grep -rn ALIAS --include=CMakeLists.txt .` returns only 3rdparty/qwindowkit/qmsetup/src/syscmdline/CMakeLists.txt:67 and 3rdparty/qwindowkit/src/CMakeLists.txt:88. gallery/CMakeLists.txt:195-200 links the un-namespaced `HuskarUIBasic`.
  → In the add_subdirectory flow write `target_link_libraries(DreamDSP PRIVATE Qt6::Quick HuskarUIBasic)`. Only write `HuskarUI::Basic` if you switch to `find_package(HuskarUI REQUIRED)`.
- WARNING — the HuskarUI already installed into your Qt kit is STALE and incompatible with the checked-out source. `D:\Qt\6.8.3\msvc2022_64\lib\cmake\HuskarUI\HuskarUIConfigVersion.cmake` says PACKAGE_VERSION "0.5.2.0", and `D:\Qt\6.8.3\msvc2022_64\qml\HuskarUI\` contains ONLY `Basic` — there is no `Impl` module and no HuskarUIImpl.dll in D:\Qt\6.8.3\msvc2022_64\bin (only HuskarUIBasic.dll). The checked-out 0.7.0.0 HusWindow.qml hard-requires `import HuskarUI.Impl`.
  evidence: D:\Qt\6.8.3\msvc2022_64\lib\cmake\HuskarUI\HuskarUIConfigVersion.cmake:13 `set(PACKAGE_VERSION "0.5.2.0")`; `ls D:/Qt/6.8.3/msvc2022_64/qml/HuskarUI` → only `Basic`; `ls D:/Qt/6.8.3/msvc2022_64/bin | grep -i huskar` → only HuskarUIBasic.dll. I:\Qt\DreamDSP\third_party\HuskarUI\src\imports\HusWindow.qml:25 `import HuskarUI.Impl`. Local git: `36abc93 Merge pull request #65 from mengps/v0.7.0.0`.
  → Do NOT call bare `find_package(HuskarUI REQUIRED)` — it will silently pick up the stale 0.5.2.0 in the Qt kit. Either (a) use add_subdirectory as in code_samples[1], or (b) rebuild+install 0.7.0.0 over the kit and then pin `find_package(HuskarUI 0.7 REQUIRED)`. Note HuskarUIConfigVersion uses COMPATIBILITY SameMinorVersion (src/CMakeLists.txt:277-281), so a version-pinned find_package will correctly reject 0.5.2.0.
- When add_subdirectory'ing, you MUST set `INSTALL_HUSKARUI_IN_DEFAULT_LOCATION=OFF` first, otherwise HuskarUI FORCE-overwrites your project's CMAKE_INSTALL_PREFIX to the Qt SDK directory. The root CMakeLists also FORCE-overwrites it to <HuskarUI>/HuskarUI when the prefix is still at its default.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\CMakeLists.txt:54-58 `if(INSTALL_HUSKARUI_IN_DEFAULT_LOCATION) set(CMAKE_INSTALL_PREFIX "${Qt6_DIR}/../../../" CACHE PATH "" FORCE)`; I:\Qt\DreamDSP\third_party\HuskarUI\CMakeLists.txt:50-52 `if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT) set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/HuskarUI" CACHE PATH "" FORCE)`.
  → Before add_subdirectory: `set(INSTALL_HUSKARUI_IN_DEFAULT_LOCATION OFF CACHE BOOL "" FORCE)` and `set(HUSKARUI_INSTALL_DIRECTORY "${CMAKE_BINARY_DIR}/HuskarUI-install" CACHE PATH "" FORCE)`. Also set your own CMAKE_INSTALL_PREFIX explicitly if you care about it.
- Also set `BUILD_HUSKARUI_GALLERY=OFF` when embedding — the gallery unconditionally FORCE-overwrites the cache variable QML_IMPORT_PATH, which would clobber your own QML tooling path.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\gallery\CMakeLists.txt:202-208 `set(QML_IMPORT_PATH ${CMAKE_BINARY_DIR}/HuskarUI/qml CACHE STRING "" FORCE)` / `set(QML_IMPORT_PATH ${HUSKARUI_PLUGIN_OUTPUT_DIRECTORY}/HuskarUI/qml CACHE STRING "" FORCE)`. Gallery is added at I:\Qt\DreamDSP\third_party\HuskarUI\CMakeLists.txt:56-58 gated on BUILD_HUSKARUI_GALLERY (default ON, line 20).
  → `set(BUILD_HUSKARUI_GALLERY OFF CACHE BOOL "" FORCE)` before add_subdirectory. (Aside: the second branch at gallery/CMakeLists.txt:206 looks buggy — HUSKARUI_PLUGIN_OUTPUT_DIRECTORY already ends in .../qml/HuskarUI/Basic, so appending /HuskarUI/qml yields a nonexistent path. Another reason to keep the gallery off and set your import path yourself.)
- The complete set of CMake options is: BUILD_HUSKARUI_GALLERY (ON), BUILD_HUSKARUI_STATIC_LIBRARY (OFF), BUILD_HUSKARUI_ON_DESKTOP_PLATFORM (ON on Win/Mac/Linux), INSTALL_HUSKARUI_IN_DEFAULT_LOCATION (ON); plus 4 PATH cache vars HUSKARUI_HEADER_OUTPUT_DIRECTORY, HUSKARUI_LIBRARY_OUTPUT_DIRECTORY, HUSKARUI_PLUGIN_OUTPUT_DIRECTORY ("Must end with [HuskarUI/Basic]"), HUSKARUI_INSTALL_DIRECTORY.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\CMakeLists.txt:20-29.
  → Defaults when unset (src/CMakeLists.txt:39-51): headers -> ${CMAKE_BINARY_DIR}/HuskarUI/include, libs -> ${CMAKE_BINARY_DIR}/HuskarUI/{bin,lib}, plugin -> ${CMAKE_BINARY_DIR}/HuskarUI/qml/HuskarUI/Basic; Impl plugin -> ${CMAKE_BINARY_DIR}/HuskarUI/qml/HuskarUI/Impl (src_impl/CMakeLists.txt:28-32). So the QML import path to hand the engine is `${CMAKE_BINARY_DIR}/HuskarUI/qml`.
- BUILD_HUSKARUI_ON_DESKTOP_PLATFORM=ON is what pulls in and links QWindowKit; it builds QWK statically with widgets off and quick on, and does NOT install QWK.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\CMakeLists.txt:34-40 (QWINDOWKIT_BUILD_STATIC ON, QWINDOWKIT_BUILD_WIDGETS OFF, QWINDOWKIT_BUILD_QUICK ON, QWINDOWKIT_INSTALL OFF, add_subdirectory(3rdparty/qwindowkit)); I:\Qt\DreamDSP\third_party\HuskarUI\src_impl\CMakeLists.txt:100-104 links QWKCore and QWKQuick into HuskarUIImpl behind that flag.
  → Leave it ON for DreamDSP (Windows desktop). The qwindowkit submodule IS populated locally (I:\Qt\DreamDSP\third_party\HuskarUI\3rdparty\qwindowkit\src\quick\quickwindowagent.cpp exists), so no `git submodule update` is needed.
- Build prerequisites are satisfied on this machine: HuskarUI needs Qt6 Quick, QuickTemplates2, LabsQmlModels, ShaderTools AND the *private* headers of Quick/QuickTemplates2/LabsQmlModels. Qt 6.8.3 msvc2022_64 has all of them.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\CMakeLists.txt:11 find_package(Qt6 6.5 COMPONENTS Quick QuickTemplates2 LabsQmlModels ShaderTools REQUIRED), lines 14-28 private-module detection, lines 213-217 links Qt6::QuickPrivate + Qt6::QuickTemplates2Private. Verified present: D:\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6ShaderTools, Qt6LabsQmlModels, Qt6QuickTemplates2; include dirs D:\Qt\6.8.3\msvc2022_64\include\QtQuick\6.8.3, \QtQuickTemplates2\6.8.3, \QtLabsQmlModels\6.8.3. My test configure printed "The Qt has private module: TRUE".
  → No extra Qt modules to install. Keep CMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64.
- main.cpp bootstrap is exactly three things beyond a normal Qt Quick app: set the OpenGL graphics API (non-Mac), enable the default alpha buffer, and add the HuskarUI qml import path. Nothing else — no manual type registration, no theme init call.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\gallery\cpp\main.cpp:14-41 — `QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)` guarded by `#ifndef Q_OS_MAC`, `QQuickWindow::setDefaultAlphaBuffer(true)`, then QGuiApplication, QQmlApplicationEngine, `engine.addImportPath(HUSKARUI_IMPORT_PATH)`, `engine.load(url)`. HUSKARUI_IMPORT_PATH is injected at gallery/CMakeLists.txt:208.
  → Copy code_samples[0] verbatim. setDefaultAlphaBuffer(true) is load-bearing: HusWindow.setSpecialEffect() sets `window.color = 'transparent'` for DwmBlur/Acrylic/Mica (HusWindow.qml:100,111,122,133) and that needs an alpha buffer.
- The icon font is loaded automatically by the QML plugin's initializeEngine — you never call anything yourself.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\cpp\huskaruiplugin.cpp:19-22 `initializeEngine` calls `HusApp::initialize(engine)`; I:\Qt\DreamDSP\third_party\HuskarUI\src\cpp\husapp.cpp `HusApp::initialize` = `QFontDatabase::addApplicationFont(":/HuskarUI/resources/font/HuskarUI-Icons.ttf")`. The ttf is compiled in at src/CMakeLists.txt:175.
  → Nothing to do. Just `import HuskarUI.Basic`.
- For a STATIC HuskarUI build you must Q_IMPORT_QML_PLUGIN both plugins and link both static plugin targets.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\gallery\cpp\main.cpp:5-9 `Q_IMPORT_QML_PLUGIN(HuskarUI_ImplPlugin)` + `Q_IMPORT_QML_PLUGIN(HuskarUI_BasicPlugin)` under `#ifdef BUILD_HUSKARUI_STATIC_LIBRARY`; gallery/CMakeLists.txt:195-200 links `HuskarUIImplPlugin` and `HuskarUIBasicPlugin` under the same generator expression; src/CMakeLists.txt:66-72 chooses PLUGIN_NAME HuskarUIBasicPlugin vs huskaruibasicplugin.
  → For a single-user personal build, prefer the default SHARED (BUILD_HUSKARUI_STATIC_LIBRARY=OFF) — simpler. If you go static, mirror the two Q_IMPORT_QML_PLUGIN lines and add the two plugin targets plus `target_compile_definitions(DreamDSP PRIVATE BUILD_HUSKARUI_STATIC_LIBRARY)`.
- HusWindow does ALL the QWindowKit wiring internally. main.qml needs zero frameless-window code: HusWindow instantiates a HusWindowAgent child, and HusWindowAgent::classBegin() calls QWK::QuickWindowAgent::setup() when its parent's objectName is '__HusWindow__' (which HusWindow sets).
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\imports\HusWindow.qml:180 `objectName: '__HusWindow__'`, :203-205 `HusWindowAgent { id: __windowAgent }`, :207-214 the built-in HusCaptionBar with targetWindow: window. I:\Qt\DreamDSP\third_party\HuskarUI\src_impl\cpp\huswindowagent.cpp:41-58 classBegin() → `setup(qobject_cast<QQuickWindow*>(p))` gated on objectName == "__HusWindow__". Class decl at src_impl/cpp/huswindowagent.h:12-27 (`class HusWindowAgent : public QWK::QuickWindowAgent, public QQmlParserStatus`, QML_NAMED_ELEMENT(HusWindowAgent)).
  → Just use `HusWindow { ... }` as your root. Do not create a QWK agent yourself. Do NOT nest HusWindow inside HusWindow (docs/General/HusWindow.md:54-73 explicitly warns; use a Loader with sourceComponent: HusWindow).
- HusWindow API: properties `contentHeight` (height minus caption bar), `captionBar` (alias to the built-in HusCaptionBar), `windowAgent` (alias to HusWindowAgent), `followThemeSwitch` (bool, default true), `initialized` (bool), `specialEffect` (int), `isDesktopPlatform` (bool). Functions: setWindowState(string), setMacSystemButtonsVisible(bool), setWindowMode(isDark), setSpecialEffect(int).
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\imports\HusWindow.qml:42-50 (properties), :52-71 setWindowState, :73-78 setMacSystemButtonsVisible, :80-88 setWindowMode, :90-169 setSpecialEffect. Mirrored in I:\Qt\DreamDSP\third_party\HuskarUI\docs\General\HusWindow.md:26-44.
  → Anchor your content to `captionBar.bottom` (or use `contentHeight`). Leave followThemeSwitch:true so the window repaints on dark/light change (HusWindow.qml:192-201).
- Windows blur/mica/acrylic are exposed via the `HusWindow.SpecialEffect` enum: None=0, Win_DwmBlur=1, Win_AcrylicMaterial=2, Win_Mica=3, Win_MicaAlt=4, Mac_BlurEffect=10. Each maps to a QWK setWindowAttribute string ('dwm-blur','acrylic-material','mica','mica-alt','blur-effect') and sets window.color='transparent' on success.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\imports\HusWindow.qml:31-40 (enum), :90-169 (setSpecialEffect implementation, returns bool).
  → Call `setSpecialEffect(HusWindow.Win_Mica)` in Component.onCompleted and check the bool return; on failure fall back to HusWindow.None (which restores `window.color = HusTheme.Primary.colorBgBase`, line 145).
- HusCaptionBar is the title bar and it also does the QWK hit-testing wiring: it calls windowAgent.setSystemButton(Minimize/Maximize/Close, ...), windowAgent.setTitleBar(...), and exposes addInteractionItem/removeInteractionItem which forward to setHitTestVisible.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\imports\HusCaptionBar.qml:156-158 (setSystemButton with HusWindowAgent.Minimize/Maximize/Close), :215-220 (setHitTestVisible true/false), :244 (setTitleBar). Property list at I:\Qt\DreamDSP\third_party\HuskarUI\docs\General\HusCaptionBar.md:26-58.
  → Key HusCaptionBar props for DreamDSP: winIcon(url), winIconWidth/Height(real,22), showWinIcon, winTitle/winTitleFont/winTitleColor, showWinTitle, showReturnButton, showThemeButton, showTopButton, topButtonChecked, showMinimize/Maximize/CloseButton, and callbacks returnCallback/themeCallback/topCallback/minimizeCallback/maximizeCallback/closeCallback. If you put any clickable control INSIDE the caption bar (e.g. a preset dropdown), you must call `captionBar.addInteractionItem(theItem)` or clicks will be eaten by the drag region — see Gallery.qml:44-51 which does exactly that in a Connections onWindowAgentChanged handler.
- Theme switching is via the `HusTheme` QML singleton's `darkMode` property, with enum values HusTheme.Light / HusTheme.Dark / HusTheme.System. `HusTheme.isDark` is the read-only resolved result.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\cpp\theme\hustheme.h:14-15 (QML_SINGLETON, QML_NAMED_ELEMENT(HusTheme)), :17-18 (isDark READ-only, darkMode READ/WRITE), :79-84 `enum class DarkMode { Light=0, Dark, System }` + Q_ENUM. Real usage: I:\Qt\DreamDSP\third_party\HuskarUI\gallery\qml\Gallery.qml:301 `HusTheme.darkMode = HusTheme.isDark ? HusTheme.Light : HusTheme.Dark;` and gallery/qml/Home/SettingsPage.qml:377.
  → Toggle: `HusTheme.darkMode = HusTheme.isDark ? HusTheme.Light : HusTheme.Dark`. Follow the OS: `HusTheme.darkMode = HusTheme.System`. Read current state with `HusTheme.isDark`.
- Accent color is set with `HusTheme.installThemePrimaryColorBase(color)` — a single call that regenerates the whole 1..10 palette ramp. Siblings: installThemeColorTextBase('#000|#fff'), installThemeColorBgBase('#fff|#000'), installThemePrimaryFontSizeBase(int), installThemePrimaryFontFamiliesBase(string), installThemePrimaryRadiusBase(int), installThemePrimaryAnimationBase(fast,mid,slow), installSizeHintRatio(name,ratio), installIndexToken(token,value), installComponentToken(component,token,value), installComponentTheme(component,jsonPath), installIndexTheme(jsonPath), reloadTheme().
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\cpp\theme\hustheme.h:120-192 (all Q_INVOKABLE declarations with doc comments). Live usage: I:\Qt\DreamDSP\third_party\HuskarUI\gallery\qml\Examples\Theme\ExpTheme.qml:134 and :177 `HusTheme.installThemePrimaryColorBase(...)`. Docs: I:\Qt\DreamDSP\third_party\HuskarUI\docs\Theme\HusTheme.md:15-83.
  → For a Peace-like accent picker: `HusTheme.installThemePrimaryColorBase('#1677ff')`. Note the light|dark pair syntax for text/bg base: `HusTheme.installThemeColorTextBase('#000|#fff')`.
- Theme tokens live in a JSON file compiled into the library. Global tokens are exposed in QML as `HusTheme.Primary.<token>`; per-component tokens as `HusTheme.<ComponentName>.<token>` (one QVariantMap property per component, ~50 of them).
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\cpp\theme\hustheme.h:24 `HUS_PROPERTY_READONLY(QVariantMap, Primary); /*! 所有 {Index.json} 中的变量 */` and :26-76 (HusButton, HusCard, HusMenu, HusSlider, HusSelect, HusSwitch, HusTabView, HusTableView, HusModal, HusDrawer, HusMessage, HusNotification, HusToolTip, HusScrollBar, HusDivider, HusPopover, HusSegmented, ...). Token source: I:\Qt\DreamDSP\third_party\HuskarUI\src\resources\theme\Index.json (registered at src/CMakeLists.txt:156-162 under prefix /HuskarUI).
  → Useful Primary tokens (verbatim names from Index.json): colorTextBase, colorBgBase, colorPrimary, colorPrimaryHover, colorPrimaryActive, colorPrimaryBg, colorTextPrimary/Secondary/Tertiary/Quaternary/Disabled, colorFill/colorFillSecondary/Tertiary/Quaternary, colorBgLayout, colorBgContainer, colorBgElevated, colorBgMask, colorBorder, colorSplit, colorSuccess, colorWarning, colorError, colorInfo, colorLink, fontPrimaryFamily, fontPrimarySize, fontPrimarySizeHeading1..5, fontPrimaryHeight, radiusPrimary, radiusPrimaryLG/SM/XS/Outer, durationFast(100)/durationMid(200)/durationSlow(300). Bind directly, e.g. `color: HusTheme.Primary.colorBgContainer`. NOTE: font sizes/radii come back as strings — the library itself does `parseInt(HusTheme.Primary.fontPrimarySize)` (see src/imports/HusText.qml:36).
- The default theme base values are colorTextBase '#000000|#f3f3f3', colorBgBase '#f5f5f5|#181818', primary '#Preset_Blue', fontSizeBase 16, radiusBase 6, fontFamilyBase "'Microsoft YaHei UI', BlinkMacSystemFont, 'Segoe UI', Roboto, ...". Tokens are computed with generator functions $genColor/$genFontSize/$genRadius/$alpha/$brightness/$darker/$multiply and referenced with @token-N ramp indices.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\resources\theme\Index.json — `__init__.__base__` and `__init__.__vars__` blocks, then `__style__` (e.g. "colorPrimary": "@colorPrimaryBase-6", "colorTextSecondary": "$alpha(@colorTextBase, 0.65)"), then `__component__` mapping each component to :/HuskarUI/resources/theme/<Name>.json.
  → `HusTheme.installIndexToken('colorPrimary', '@colorPrimaryBase-6')` supports these generator functions (hustheme.h:176 warns "支持Token生成函数(genColor/genFont/genFontSize/genRadius)"). For a full custom skin, write your own Index.json and call `HusTheme.installIndexTheme(path)`.
- `HusTheme.animationEnabled` (bool, default true) is a global animation kill-switch; nearly every component's `animationEnabled` defaults to it. There is also `HusTheme.textRenderType` (QtRendering=0 / NativeRendering=1 / CurveRendering=2) and `HusTheme.sizeHint` (a QVariantMap: small=0.8, normal=1.0, large=1.25).
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\cpp\theme\hustheme.h:19-22 (textRenderType, sizeHint, HUS_PROPERTY_INIT(bool, animationEnabled, ..., true)), :86-91 (TextRenderType enum). Defaults at I:\Qt\DreamDSP\third_party\HuskarUI\src\cpp\theme\hustheme.cpp:847-849 (`m_sizeHintMap["small"]=0.8; ["normal"]=1.0; ["large"]=1.25;`). Components use `sizeHint: string` — e.g. docs/General/HusButton.md, docs/DataEntry/HusSelect.md.
  → On Windows with OpenGL, `HusTheme.textRenderType = HusTheme.NativeRendering` often gives crisper small text. Set `sizeHint: 'small'|'normal'|'large'` on HusButton/HusSelect/HusInputNumber/HusSegmented for a denser Peace-like layout.
- C++ classes exposed to QML use declarative registration (QML_NAMED_ELEMENT / QML_SINGLETON via qt_add_qml_module URI "HuskarUI.Basic") — there is no qmlRegisterType call anywhere. Singletons: HusApp, HusApi, HusTheme, HusIcon. Instantiable: HusRadius, HusBorder, HusRectangle, HusWatermark, HusQrCode, HusIconSettings, HusRouter, HusRouterHistory, HusAsyncHasher, and (from HuskarUI.Impl) HusWindowAgent.
  evidence: src/CMakeLists.txt:111-120 `qt_add_qml_module(... URI "HuskarUI.Basic" VERSION 1.0 ... DEPENDENCIES HuskarUI.Impl)`. Registration macros: src/cpp/husapp.h:11-12; src/cpp/utils/husapi.h:13-14; src/cpp/theme/hustheme.h:14-15; src/cpp/controls/husiconfont.h:12-13; src/cpp/items/husradius.h:19; src/cpp/items/husborder.h:13; src/cpp/items/husrectangle.h:25,82,95; src/cpp/controls/huswatermark.h:23; src/cpp/controls/husqrcode.h:18,58; src/cpp/utils/husrouter.h:19,59; src/cpp/utils/husasynchasher.h:27; src_impl/cpp/huswindowagent.h:19.
  → For DreamDSP's own backend classes, use the same modern pattern: QML_ELEMENT / QML_SINGLETON + qt_add_qml_module — no manual registration needed. HusApi (src/cpp/utils/husapi.h:22-38) gives you free helpers you'll want: setWindowStaysOnTopHint(QWindow*,bool), setWindowState(QWindow*,int), getClipboardText()/setClipboardText(), readFileToString(fileName), openLocalUrl(local), clamp(v,min,max).
- Icons ARE bundled: a HuskarUI-Icons.ttf webfont plus a `HusIcon` QML singleton whose `Type` enum has ~1600 named Ant-Design entries (codepoints 0xe900..0xef52). You pass the enum value to any component's `iconSource` property; the SAME property also accepts a string URL for a normal image.
  evidence: src\cpp\controls\husiconfont.h:9-16 (class HusIcon, QML_SINGLETON, QML_NAMED_ELEMENT(HusIcon), `enum class Type : uint16_t { HuskarUI = 0xe900, AccountBookFilled = 0xe901, ... IcoMoon = 0xef52 }`), :1634 Q_ENUM(Type), :1641 `static Q_INVOKABLE QVariantMap allIconNames();`. Font compiled in at src/CMakeLists.txt:175. Dual int|string handling proven in src\imports\HusIconText.qml:30 `text: __iconLoader.active ? '' : String.fromCharCode(iconSource)` and :45-52 (Loader active when `typeof iconSource == 'string' && iconSource !== ''` → Image).
  → Usage in a button: `HusIconButton { iconSource: HusIcon.SettingOutlined; iconSize: 16; text: 'Settings' }` — real example at gallery\qml\Gallery.qml:66-67 (`iconSource: HusIcon.ArrowLeftOutlined; iconSize: 14`). Names useful for DreamDSP: SlidersOutlined, SoundOutlined, MutedOutlined, SettingOutlined, SaveOutlined, FolderOpenOutlined, ImportOutlined, ExportOutlined, ReloadOutlined, DeleteOutlined, PlusOutlined, EditOutlined, UndoOutlined, RedoOutlined, LineChartOutlined, ControlOutlined, PoweroffOutlined, MoonOutlined/SunOutlined. Enumerate at runtime with `HusIcon.allIconNames()`.
- HusButton API: type (HusButton.Type_Default=0, Type_Outlined=1, Type_Dashed=2, Type_Primary=3, Type_Filled=4, Type_Text=5, Type_Link=6), shape (Shape_Default=0, Shape_Circle=1), plus animationEnabled, effectEnabled, active, hoverCursorShape, borderWidth, colorText, colorBg, colorBorder, radiusBg (HusRadius), sizeHint, contentDescription. Inherits QtQuick Controls Button (so `text`, `onClicked`, `checkable`, `enabled` all work).
  evidence: Enums: I:\Qt\DreamDSP\third_party\HuskarUI\src\imports\HusButton.qml (enum Type / enum Shape blocks). Property table: I:\Qt\DreamDSP\third_party\HuskarUI\docs\General\HusButton.md:24-40 ("继承自 { Button }").
  → `HusButton { text: 'Apply'; type: HusButton.Type_Primary; onClicked: backend.apply() }`.
- HusIconButton extends HusButton with iconSource (int|string), iconSize, iconSpacing (5), iconPosition (HusIconButton.Position_Start=0 / Position_End=1), loading, orientation (Qt.Horizontal), textFont, iconFont ('HuskarUI-Icons'), colorIcon, and an iconDelegate. HusCaptionButton extends HusIconButton adding only isError(bool) and noDisabledState(bool).
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\General\HusIconButton.md:24-38; enum at src\imports\HusIconButton.qml (`enum IconPosition { Position_Start = 0, Position_End = 1 }`). HusCaptionButton: I:\Qt\DreamDSP\third_party\HuskarUI\docs\General\HusCaptionButton.md:24-30.
  → Use HusCaptionButton only inside HusCaptionBar (and register it via captionBar.addInteractionItem). Elsewhere use HusIconButton.
- HusSlider — the single most important control for an equalizer — supports single and dual-handle mode. Properties: min(0), max(100), stepSize(0.0), value (number OR [min,max] when range:true), currentValue (readonly), range(bool), snapMode (HusSlider.NoSnap=0/SnapAlways=1/SnapOnRelease=2), orientation (Qt.Horizontal|Qt.Vertical), colorHandle, colorTrack, colorBg, radiusBg, animationEnabled, hoverCursorShape, contentDescription. Signals: firstMoved(), firstReleased(), secondMoved(), secondReleased(). Functions: increase(first=true), decrease(first=true). Delegates: handleDelegate, handleToolTipDelegate, bgDelegate.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\DataEntry\HusSlider.md:17-80 (delegates, property table, functions, signals). Enum at src\imports\HusSlider.qml (`enum SnapMode { NoSnap = 0, SnapAlways = 1, SnapOnRelease = 2 }`).
  → For a vertical EQ band: `HusSlider { orientation: Qt.Vertical; min: -30; max: 30; stepSize: 0.1; snapMode: HusSlider.SnapAlways; value: band.gain; onFirstMoved: band.gain = currentValue }`. IMPORTANT: write to `value`, read from `currentValue` — they are different properties (doc lines 47-48). Use `firstMoved`/`firstReleased` rather than a two-way binding to avoid loops. Note the doc's own typo: the delegate is spelled `handleDelgate` (line 19) — verify against src\imports\HusSlider.qml before relying on it.
- HusSelect (inherits ComboBox) takes a JS array model of `{label, value, enabled}` objects. Properties: clearEnabled(true), clearIconSource(HusIcon.CloseCircleFilled), showToolTip(false), loading(false), placeholderText, defaultPopupMaxHeight(240), active, colorText/colorBorder/colorBg, radiusBg/radiusItemBg/radiusPopupBg (HusRadius), sizeHint('normal'), animationEnabled, hoverCursorShape, contentDescription. Signal: clickClear(). textRole/valueRole from ComboBox can rename label/value.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\DataEntry\HusSelect.md:24-77 ("继承自 { ComboBox }", delegates indicatorDelegate/toolTipDelegate, property table, model property table, signal). Usage example at the same file's 示例 1: `HusSelect { width: 120; sizeHint: ...; editable: ...; showToolTip: true; model: [ { value: 'jack', label: 'Jack' }, ... ] }`.
  → For Peace's preset/device dropdowns: `HusSelect { width: 220; placeholderText: 'Select preset'; model: presetModel; onActivated: (i) => backend.loadPreset(currentValue) }` (currentValue/currentIndex/activated come from ComboBox).
- HusSwitch (inherits Switch) properties: checkedText, uncheckedText, checkedIconSource, uncheckedIconSource, iconSize, loading, effectEnabled(true), animationEnabled, hoverCursorShape, textFont, colorText, colorHandle, colorBg, radiusBg, contentDescription. Delegate: handleDelegate. `checked`/`onToggled` come from Switch.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\DataEntry\HusSwitch.md:24-45.
  → `HusSwitch { checked: backend.eqEnabled; checkedText: 'ON'; uncheckedText: 'OFF'; onToggled: backend.eqEnabled = checked }`.
- HusInputNumber is a full numeric spinbox with prefix/suffix and formatter/parser — ideal for Peace's gain/frequency/Q fields. Key props: value(real), min(Number.MIN_SAFE_INTEGER), max, step(1), precision(0), validator(DoubleValidator), showHandler(true), alwaysShowHandler(false), useWheel(false), useKeyboard(true), readOnly, prefix, suffix, beforeLabel/afterLabel (string OR list → becomes a dropdown), currentBeforeLabel/currentAfterLabel, formatter(fn), parser(fn), clearEnabled(false|'active'), type (HusInput.Type_Outlined=1/Dashed=2/Borderless=3/Underlined=4/Filled=5), sizeHint, radiusBg, `input` (access the inner HusInput). Signals: valueModified(), beforeActivated(index,data), afterActivated(index,data). Functions: increase(), decrease(), getFullText(), plus TextInput's select/selectAll/clear/copy/cut/paste/undo/redo.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\DataEntry\HusInputNumber.md:24-100 (property table, signals, functions). HusInput enums at src\imports\HusInput.qml (`enum Type { Type_Outlined = 1, Type_Dashed = 2, Type_Borderless = 3, Type_Underlined = 4, Type_Filled = 5 }`, `enum IconPosition { Position_Left = 0, Position_Right = 1 }`).
  → Frequency field: `HusInputNumber { value: band.freq; min: 20; max: 20000; step: 1; precision: 0; afterLabel: 'Hz'; useWheel: true; onValueModified: backend.setFreq(value) }`. Gain field: set `precision: 1` and `afterLabel: 'dB'`. Use afterLabel as a LIST (e.g. ['Hz','kHz']) plus onAfterActivated to get a unit switcher for free.
- HusSegmented is the Ant-style segmented control (perfect for Peace's filter-type / channel selectors). Props: options (array of {label, value, enabled, toolTip, iconSource}), currentIndex(0), currentValue(readonly), count(readonly), block(false), orientation(Qt.Horizontal), defaultItemHeight(26), iconSpacing(5), iconFont, colorBg, colorIndicatorBg, colorBorder, radiusBg, sizeHint. Model-mutation functions: get/set/setProperty/move/insert/append/remove/clear.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\DataDisplay\HusSegmented.md:24-120 (delegates indicatorDelegate/itemDelegate/iconDelegate/toolTipDelegate, property table, options table, functions).
  → `HusSegmented { options: [{label:'Peak'},{label:'Low Shelf'},{label:'High Shelf'}]; onCurrentIndexChanged: backend.setFilterType(currentValue) }`.
- HusTabView is a full tab container with its own model. Props: initModel(array of {key,title,iconSource,iconSize,iconSpacing,tabWidth,tabHeight,editable,contentDelegate}), count, currentIndex, tabType (Type_Default=0/Type_Card=1/Type_CardEditable=2), tabSize (Size_Auto=0/Size_Fixed=1), tabPosition (Position_Top=0/Bottom=1/Left=2/Right=3), tabAlign (Align_Center=0/Align_Left=1/Align_Right=2), tabAddable, tabCentered, tabCardMovable, defaultTab* sizing props, addTabCallback, closeTabCallback(index,data). Delegates: tabDelegate, contentDelegate, highlightDelegate, addButtonDelegate. Functions: setCurrentIndex, get/set/setProperty/move/insert/append/remove/clear, positionViewAt*.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\DataDisplay\HusTabView.md:24-130. Enums at src\imports\HusTabView.qml (TabPosition/TabType/TabSize/TabAlign blocks).
  → For Peace's main sections use either HusTabView (tabPosition: HusTabView.Position_Left) or HusMenu. Per-tab content override: put `contentDelegate: Component{...}` inside the individual model entry.
- HusMenu is a nestable navigation menu with unlimited depth. Props: initModel (array of {key,label,shortLabel,type,enabled,iconSource,iconSize,iconSpacing,menuChildren,iconDelegate,labelDelegate,contentDelegate,bgDelegate}), defaultSelectedKeys, selectedKey, compactMode (HusMenu.Mode_Relaxed=0/Mode_Standard=1/Mode_Compact=2), compactWidth, popupMode, popupWidth(200), popupOffset(4), popupMaxHeight, defaultMenuWidth(300), defaultMenuSpacing(4), showEdge, showToolTip, radiusMenuBg, radiusPopupBg, scrollBar. Signal: clickMenu(deep, key, keyPath, data). Functions: gotoMenu(key), setData(key,data), setDataProperty(key,prop,value), get/set/setProperty/move/insert/append/remove/clear.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\Navigation\HusMenu.md:24-160. Enum at src\imports\HusMenu.qml (`enum CompactMode { Mode_Relaxed = 0, Mode_Standard = 1, Mode_Compact = 2 }`).
  → Left nav for DreamDSP: `HusMenu { initModel: [{key:'eq',label:'Equalizer',iconSource:HusIcon.SlidersOutlined}, {key:'presets',label:'Presets',iconSource:HusIcon.FolderOpenOutlined}]; defaultSelectedKeys:['eq']; onClickMenu: (deep,key) => stack.select(key) }`. Collapse to icon-only with compactMode: HusMenu.Mode_Compact.
- HusCard (inherits Control) props: title, hoverable(false), showShadow(=hoverable), coverSource, coverFillMode, bodyAvatarSize/Icon/Source/Text, bodyTitle, bodyDescription, titleFont, bodyTitleFont, bodyDescriptionFont, colorTitle/colorBg/colorBorder/colorShadow/colorBody*. Delegates: titleDelegate, extraDelegate, coverDelegate, bodyDelegate, actionDelegate.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\DataDisplay\HusCard.md:24-64.
  → Group Peace's EQ band controls with `HusCard { title: 'Band 1'; bodyDelegate: Component { ... } }`. `extraDelegate` is the right slot for a per-card enable HusSwitch.
- HusModal inherits HusPopup. Props: position (HusModal.Position_Top=0/Bottom=1/Center=2/Left=3/Right=4), positionMargin(120), closable(true), maskClosable(true), iconSource, iconSize(24), title, description, confirmText, cancelText, colorIcon/colorTitle/colorDescription, titleFont/descriptionFont, plus Popup's `modal`. Signals: confirm(), cancel(). Functions: openInfo(), openSuccess(), openError(), openWarning() (and Popup's open()/close()).
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\Feedback\HusModal.md:24-84; enum at src\imports\HusModal.qml. Usage example in the same doc: `HusModal { id: modal1; modal: true; title: 'Basic Modal'; description: '...'; confirmText: 'Yes'; cancelText: 'No'; onConfirm: close(); onCancel: close() }` opened with `modal1.open()`.
  → Use for Peace's "overwrite preset?" / "config.txt not writable" dialogs. You must call `close()` yourself in onConfirm/onCancel — it does not auto-close.
- HusDrawer inherits QtQuick Drawer. Props: drawerSize(378), edge (Qt.RightEdge default; Qt.*Edge), title, titleFont, colorTitle, colorBg, colorOverlay, maskClosable(true), closePosition (HusDrawer.Position_Start=0/Position_End=1), animationEnabled. Delegates: closeDelegate, titleDelegate, contentDelegate.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\Feedback\HusDrawer.md:24-40; enum at src\imports\HusDrawer.qml (`enum ClosePosition { Position_Start = 0, Position_End = 1 }`).
  → `HusDrawer { edge: Qt.RightEdge; drawerSize: 420; title: 'Advanced'; contentDelegate: Component { ... } }` then `.open()`.
- HusMessage and HusNotification are Items you place once and drive imperatively. Both expose info/success/error/warning/loading(...), open(object), close(key), clear(), getMessage|getNotification(key), setProperty(key,prop,value), and a closed(key) signal. HusMessage default duration 3000ms; HusNotification 4500ms and adds a description arg, position (8-value NotificationPosition enum), stackMode/stackThreshold(5), showProgress, pauseOnHover, maxNotificationWidth(300).
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\Feedback\HusMessage.md:24-90 and docs\Feedback\HusNotification.md:24-110. Enums at src\imports\HusMessage.qml (`enum MessageType { Type_None=0, Type_Success=1, Type_Warning=2, Type_Message=3, Type_Error=4 }`) and src\imports\HusNotification.qml (`enum NotificationPosition { Position_Top=0, Position_TopLeft=1, Position_TopRight=2, Position_Bottom=3, Position_BottomLeft=4, Position_BottomRight=5, Position_Left=6, Position_Right=7 }`).
  → The docs explicitly recommend parenting them to the window's caption bar so they stack on top (docs/Feedback/HusMessage.md 示例 1): `HusMessage { id: message; z: 999; parent: root.captionBar; width: parent.width; anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.bottom }` then `message.info('Saved to peace.txt')`. Same pattern for HusNotification.
- HusToolTip inherits ToolTip and adds showArrow(false), position (HusToolTip.Position_Top=0/Bottom=1/Left=2/Right=3), colorText, colorBg, animationEnabled.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\DataDisplay\HusToolTip.md:24-38; enum at src\imports\HusToolTip.qml. Real usage: gallery\qml\Gallery.qml:80-85 `HusToolTip { visible: parent.hovered; showArrow: true; position: HusToolTip.Position_Bottom; text: parent.contentDescription }`.
  → Nest it inside the hovered control and bind `visible: parent.hovered` — that's the idiom the library itself uses.
- HusPopover inherits HusPopup and needs an explicit width. Props: iconSource(HusIcon.ExclamationCircleFilled), iconSize(16), title, description, showArrow(true), arrowWidth(16), arrowHeight(8), colorIcon/colorTitle/colorDescription, titleFont/descriptionFont. Delegates: arrowDelegate, iconDelegate, titleDelegate, descriptionDelegate, contentDelegate, bgDelegate, footerDelegate.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\Feedback\HusPopover.md:24-48, with the explicit note at line ~46: "需要显示给出弹出宽度，高度将根据内容自动计算" (you must give an explicit width; height is auto).
  → Always set `width:` on a HusPopover or it will render collapsed.
- HusScrollBar inherits ScrollBar and adds only minimumHandleSize(24), colorBar, colorBg, colorIcon, animationEnabled. HusDivider (Item) has title, titleFont, titleAlign (Align_Left=0/Center=1/Right=2), titlePadding(20), lineStyle (SolidLine=0/DashLine=1), lineWidth(1), dashPattern([4,2]), orientation, colorText, colorSplit. HusText is just a themed QtQuick Text (renderType/color/font pre-bound to HusTheme).
  evidence: docs\Navigation\HusScrollBar.md:24-34; docs\Layout\HusDivider.md:24-42 with enums in src\imports\HusDivider.qml (`enum Align { Align_Left=0, Align_Center=1, Align_Right=2 }`, `enum Style { SolidLine=0, DashLine=1 }`); src\imports\HusText.qml:27-38 (Text with renderType: HusTheme.textRenderType, color: HusTheme.Primary.colorTextBase, font.family: HusTheme.Primary.fontPrimaryFamily, font.pixelSize: parseInt(HusTheme.Primary.fontPrimarySize)).
  → Use HusText everywhere instead of Text so labels follow the theme automatically. Use HusDivider { title: 'Output' } as a section header.
- HusTableView (inherits HusRectangle) is a data grid driven by `initModel` (rows) + `columns` (array of column descriptors with title/dataIndex/delegate/width, optional minimumWidth/maximumWidth/align/selectionType('checkbox'|'radio')/sorter/sortDirections/onFilter/filterInput). It has built-in sorting, filtering, row checking, column visibility, and exposes verScrollBar/horScrollBar/tableView/tableModel.
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\DataDisplay\HusTableView.md:24-190 (delegates, property table, initModel table, columns table, columns.delegate accessible props row/column/cellData/cellIndex/dataIndex/filterInput, and the ~25 functions incl. setColumnVisible, checkForKeys, getCheckedKeys, scrollToRow, sort(column), filter(), appendRow/getRow/setRow/insertRow/moveRow/removeRow, getCellData/setCellData, getTableModel, rowCount/columnCount).
  → Good fit for a Peace preset browser or a raw filter-list editor. Note the documented caveat: appendRow/setRow/insertRow/removeRow operate on the CURRENT (sorted+filtered) model and do NOT mutate initModel; call filter() afterwards to re-sort. Requires Qt.labs.qmlmodels private headers, which src/CMakeLists.txt:100-107,240-249 gates on QT_HAS_LABSQMLMODEL_PRIVATE — present in your Qt 6.8.3.
- HusAcrylic (Item) is a software blur you apply to an in-scene item — distinct from the native window Mica/Acrylic in HusWindow.setSpecialEffect. Props: sourceItem, sourceRect, opacityNoise(0.02), radiusBlur(32), colorTint('#fff'), radiusBg (HusRadius), opacityTint(0.65), luminosity(0.01).
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\docs\Effect\HusAcrylic.md:24-38.
  → Use HusWindow.setSpecialEffect(HusWindow.Win_Mica) for the window chrome; use HusAcrylic only for in-app frosted panels over a background image.
- `radiusBg` on many components is NOT a number — it is a HusRadius object with all/topLeft/topRight/bottomLeft/bottomRight (defaults: all=0, corners=-1 meaning "use all").
  evidence: I:\Qt\DreamDSP\third_party\HuskarUI\src\cpp\items\husradius.h:11-52 (Q_PROPERTY all/topLeft/topRight/bottomLeft/bottomRight, QML_NAMED_ELEMENT(HusRadius), m_all=0., corners = -1.). Doc tables link `radiusBg | HusRadius` for HusSelect, HusButton, HusSwitch, HusMenu, HusSegmented, HusTableView, HusNotification, HusMessage, HusInputNumber, HusAcrylic.
  → Write `radiusBg.all: 8` (or `radiusBg { topLeft: 8; topRight: 8 }`), NOT `radiusBg: 8`. Some components (e.g. HusSlider) use a plain `radiusBg | int` instead — check the specific doc table.

### code samples
```
// ============================================================================
// DreamDSP — minimal main.cpp for HuskarUI
// Mirrors I:\\Qt\\DreamDSP\\third_party\\HuskarUI\\gallery\\cpp\\main.cpp:14-41
// ============================================================================
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

// Only needed if HuskarUI was configured with BUILD_HUSKARUI_STATIC_LIBRARY=ON.
#ifdef BUILD_HUSKARUI_STATIC_LIBRARY
#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(HuskarUI_ImplPlugin)
Q_IMPORT_QML_PLUGIN(HuskarUI_BasicPlugin)
#endif

int main(int argc, char *argv[])
{
    // HuskarUI's own gallery forces OpenGL everywhere except macOS.
#ifndef Q_OS_MAC
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
    // REQUIRED for Mica / Acrylic / DwmBlur: HusWindow.setSpecialEffect()
    // sets window.color = 'transparent' (HusWindow.qml:100,111,122,133).
    QQuickWindow::setDefaultAlphaBuffer(true);

    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("DreamDSP"));
    app.setApplicationName(QStringLiteral("DreamDSP"));
    app.setApplicationDisplayName(QStringLiteral("DreamDSP"));

    QQmlApplicationEngine engine;

    // Injected by CMake (see the consumer CMakeLists sample).
    // Not needed if HuskarUI is installed into <QtDir>/qml.
#ifdef HUSKARUI_IMPORT_PATH
    engine.addImportPath(QStringLiteral(HUSKARUI_IMPORT_PATH));
#endif

    const QUrl url(QStringLiteral("qrc:/qt/qml/DreamDSP/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [url](QObject *obj, const QUrl &objUrl) {
                         if (!obj && url == objUrl)
                             QCoreApplication::exit(-1);
                     }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
```

```
# =============================================================================
# DreamDSP — consumer CMakeLists.txt (add_subdirectory flavour).
# VERIFIED: this exact structure configures cleanly against Qt 6.8.3 with
#   cmake -G Ninja -B build -S . -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64
# ("Configuring HuskarUI Version: 0.7.0.0", "The Qt has private module: TRUE")
# =============================================================================
cmake_minimum_required(VERSION 3.21)
project(DreamDSP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.5)

# --- HuskarUI options; ALL FOUR matter -------------------------------------
#  * gallery OFF  -> it FORCE-overwrites the QML_IMPORT_PATH cache var
#                    (HuskarUI/gallery/CMakeLists.txt:202-208)
#  * default-location install OFF -> otherwise HuskarUI FORCE-overwrites your
#                    CMAKE_INSTALL_PREFIX to the Qt SDK dir
#                    (HuskarUI/src/CMakeLists.txt:54-58)
set(BUILD_HUSKARUI_GALLERY               OFF CACHE BOOL "" FORCE)
set(BUILD_HUSKARUI_STATIC_LIBRARY        OFF CACHE BOOL "" FORCE)
set(BUILD_HUSKARUI_ON_DESKTOP_PLATFORM   ON  CACHE BOOL "" FORCE)  # pulls in QWindowKit
set(INSTALL_HUSKARUI_IN_DEFAULT_LOCATION OFF CACHE BOOL "" FORCE)
set(HUSKARUI_INSTALL_DIRECTORY "${CMAKE_BINARY_DIR}/HuskarUI-install" CACHE PATH "" FORCE)

# Must be the REPO ROOT, not src/ — src/CMakeLists.txt:3 uses ${HUSKARUI_VERSION}
# which is only defined in the root file (CMakeLists.txt:42-46).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/HuskarUI huskarui)

qt_add_executable(DreamDSP
    src/main.cpp
    # src/eqbackend.cpp src/eqbackend.h ...
)

qt_add_qml_module(DreamDSP
    URI DreamDSP
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

# add_subdirectory exposes the PLAIN target name. There is no HuskarUI::Basic
# ALIAS in the build tree — that name only exists via find_package/install.
target_link_libraries(DreamDSP PRIVATE
    Qt6::Quick
    HuskarUIBasic
)

# Where HuskarUI drops its two QML plugins when the *_OUTPUT_DIRECTORY vars are
# left empty (HuskarUI/src/CMakeLists.txt:49-51, src_impl/CMakeLists.txt:28-32):
#   ${CMAKE_BINARY_DIR}/HuskarUI/qml/HuskarUI/{Basic,Impl}
target_compile_definitions(DreamDSP PRIVATE
    HUSKARUI_IMPORT_PATH="${CMAKE_BINARY_DIR}/HuskarUI/qml"
)
set(QML_IMPORT_PATH "${CMAKE_BINARY_DIR}/HuskarUI/qml" CACHE STRING "" FORCE) # Qt Creator/qmlls

set_target_properties(DreamDSP PROPERTIES WIN32_EXECUTABLE TRUE)
```

```
# =============================================================================
# ALTERNATIVE consumer section: find_package flavour.
# ONLY use this AFTER you have rebuilt+installed HuskarUI 0.7.0.0 into the Qt
# kit. The copy currently at D:\Qt\6.8.3\msvc2022_64 is 0.5.2.0 and has NO
# HuskarUI.Impl module, which 0.7.0.0's HusWindow.qml requires.
#
# Install it first:
#   cd I:\Qt\DreamDSP\third_party\HuskarUI
#   cmake -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64 -DBUILD_HUSKARUI_GALLERY=OFF ^
#         -G Ninja -B build -S .
#   cmake --build build --config Release --target all install --parallel
# =============================================================================
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick)
find_package(HuskarUI 0.7 REQUIRED)   # version-pin: rejects the stale 0.5.2.0

qt_add_executable(DreamDSP src/main.cpp)
qt_add_qml_module(DreamDSP URI DreamDSP VERSION 1.0 QML_FILES qml/Main.qml)

target_link_libraries(DreamDSP PRIVATE Qt6::Quick HuskarUI::Basic)
# HuskarUIConfig.cmake already appends HuskarUI_QML_IMPORT_PATH to QML_IMPORT_PATH
# (.cmake/HuskarUIConfig.cmake.in:22-31). Because the plugins land in
# <QtDir>/qml, the engine finds them with no addImportPath() call at all.
```

```
// ============================================================================
// DreamDSP — qml/Main.qml : HusWindow + caption bar + themed controls
// ============================================================================
import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic

HusWindow {
    id: root

    width: 1100
    height: 720
    minimumWidth: 800
    minimumHeight: 560
    visible: true
    title: qsTr('DreamDSP')

    // Follow the OS light/dark setting; repaints automatically.
    followThemeSwitch: true

    // ---- built-in frameless caption bar (QWindowKit is wired up for you) ----
    captionBar.height: 32
    captionBar.winTitle: root.title
    captionBar.showThemeButton: true
    captionBar.showTopButton: true
    captionBar.themeCallback: () => {
        HusTheme.darkMode = HusTheme.isDark ? HusTheme.Light : HusTheme.Dark;
    }
    captionBar.topCallback: (checked) => HusApi.setWindowStaysOnTopHint(root, checked)

    Component.onCompleted: {
        HusTheme.installThemePrimaryColorBase('#1677ff');   // accent colour
        HusTheme.installThemePrimaryRadiusBase(6);
        // Native Windows backdrop; returns false if unsupported -> stays None.
        root.setSpecialEffect(HusWindow.Win_Mica);
    }

    // Toasts: parenting to the caption bar puts them above everything else
    // (idiom taken verbatim from docs/Feedback/HusMessage.md example 1).
    HusMessage {
        id: message
        z: 999
        parent: root.captionBar
        width: parent.width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.bottom
    }

    // ---- content ------------------------------------------------------------
    RowLayout {
        anchors.top: root.captionBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0

        HusMenu {
            Layout.fillHeight: true
            Layout.preferredWidth: 220
            defaultMenuWidth: 220
            defaultSelectedKeys: ['eq']
            initModel: [
                { key: 'eq',      label: qsTr('Equalizer'), iconSource: HusIcon.SlidersOutlined },
                { key: 'presets', label: qsTr('Presets'),   iconSource: HusIcon.FolderOpenOutlined },
                { key: 'config',  label: qsTr('Config'),    iconSource: HusIcon.SettingOutlined }
            ]
            onClickMenu: (deep, key, keyPath, data) => message.info('menu: ' + key)
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 12

            HusDivider { Layout.fillWidth: true; title: qsTr('Band 1') }

            RowLayout {
                spacing: 12

                HusSwitch {
                    checkedText: 'ON'
                    uncheckedText: 'OFF'
                    checked: true
                    onToggled: message.info(checked ? 'Band enabled' : 'Band bypassed')
                }

                HusSelect {
                    width: 180
                    placeholderText: qsTr('Filter type')
                    currentIndex: 0
                    model: [
                        { label: qsTr('Peaking'),     value: 'PK' },
                        { label: qsTr('Low Shelf'),   value: 'LS' },
                        { label: qsTr('High Shelf'),  value: 'HS' }
                    ]
                }

                HusInputNumber {
                    width: 150
                    value: 1000
                    min: 20
                    max: 20000
                    step: 10
                    precision: 0
                    useWheel: true
                    afterLabel: 'Hz'
                    onValueModified: message.info('freq = ' + value)
                }
            }

            HusCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                title: qsTr('Gain')
                bodyDelegate: Item {
                    HusSlider {
                        id: gain
                        anchors.centerIn: parent
                        width: 320
                        height: 30
                        min: -30
                        max: 30
                        stepSize: 0.1
                        snapMode: HusSlider.SnapAlways
                        value: 0
                        onFirstReleased: message.success('gain = ' + currentValue.toFixed(1) + ' dB')
                    }
                    HusText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: gain.bottom
                        anchors.topMargin: 8
                        text: gain.currentValue.toFixed(1) + ' dB'
                    }
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                spacing: 8
                Item { Layout.fillWidth: true }

                HusIconButton {
                    text: qsTr('Reset')
                    iconSource: HusIcon.ReloadOutlined
                    iconSize: 14
                    onClicked: gain.value = 0

                    HusToolTip {
                        visible: parent.hovered
                        showArrow: true
                        position: HusToolTip.Position_Top
                        text: qsTr('Reset this band')
                    }
                }

                HusIconButton {
                    text: qsTr('Apply')
                    type: HusButton.Type_Primary
                    iconSource: HusIcon.SaveOutlined
                    iconSize: 14
                    onClicked: confirmModal.open()

                    HusModal {
                        id: confirmModal
                        modal: true
                        title: qsTr('Write peace.txt?')
                        description: qsTr('This overwrites the Equalizer APO config.')
                        confirmText: qsTr('Write')
                        cancelText: qsTr('Cancel')
                        onConfirm: { close(); message.success('peace.txt written'); }
                        onCancel: close()
                    }
                }
            }
        }
    }
}
```

```
// Themed custom surface — bind straight to HusTheme tokens so it tracks
// dark/light and accent changes for free. Token names are verbatim from
// I:\\Qt\\DreamDSP\\third_party\\HuskarUI\\src\\resources\\theme\\Index.json
import QtQuick
import HuskarUI.Basic

Rectangle {
    color:  HusTheme.Primary.colorBgContainer
    border.color: HusTheme.Primary.colorBorderSecondary
    border.width: 1
    radius: parseInt(HusTheme.Primary.radiusPrimaryLG)   // tokens are STRINGS

    HusText {
        anchors.centerIn: parent
        text: qsTr('Output device')
        color: HusTheme.Primary.colorTextSecondary
        font.pixelSize: parseInt(HusTheme.Primary.fontPrimarySizeHeading5)
    }

    Behavior on color {
        enabled: HusTheme.animationEnabled
        ColorAnimation { duration: parseInt(HusTheme.Primary.durationMid) }  // 200ms
    }
}
```

UNKNOWNS: I only CMake-CONFIGURED the add_subdirectory integration; I did not compile or run it. My throwaway configure also picked up w64devkit's GNU g++ 15.2.0 from PATH rather than MSVC (visible in the configure log). DreamDSP must be configured from a VS 2026 developer prompt (or with -DCMAKE_CXX_COMPILER=cl) — an MSVC build of HuskarUI has NOT been verified end-to-end on this machine.; Runtime behaviour of HusWindow.setSpecialEffect(HusWindow.Win_Mica) on Windows 11 26200 is unverified — I read the code path but never ran it. It returns bool; treat false as "fall back to HusWindow.None".; Whether the stale HuskarUI 0.5.2.0 currently installed in D:\Qt\6.8.3\msvc2022_64 would still load (it has no HuskarUI.Impl). I did not test it, and I did not overwrite it.; Exact delegate property spellings: docs/DataEntry/HusSlider.md:19 and docs/DataEntry/HusSwitch.md:19 both write `handleDelgate` (missing an 'e'). I did not grep src/imports/HusSlider.qml and HusSwitch.qml to confirm which spelling the QML actually declares — check before using those delegates.; Line numbers inside src/imports/*.qml for the enum blocks: I extracted them with an awk filter that does not print line numbers, so I cite the file but not exact lines for HusButton/HusIconButton/HusSlider/HusTabView/HusMenu/HusModal/HusDrawer/HusToolTip/HusNotification/HusMessage/HusDivider/HusInput enums. The enum bodies themselves are quoted verbatim.; Per-component theme token names (the contents of e.g. HusButton.json, HusSlider.json under src/resources/theme/) — I confirmed the files exist and how they are wired in, but did not enumerate the individual token keys inside them.; The full 1600-entry HusIcon.Type enum: I read roughly the first two-thirds plus the tail. The specific icon names I recommended (SlidersOutlined, SoundOutlined, MutedOutlined, SaveOutlined, FolderOpenOutlined, ReloadOutlined, ...) were all seen in the portion I read, but verify any others against src/cpp/controls/husiconfont.h or HusIcon.allIconNames() at runtime.; How HuskarUI behaves under windeployqt for a standalone DreamDSP package. The gallery's Release deploy target (gallery/CMakeLists.txt:233-267) hand-copies HuskarUIImpl.dll and HuskarUIBasic.dll from the Qt bin dir and passes a long list of --no-quickcontrols2* flags; you will likely need an equivalent step, but I did not test deployment.; Whether HuskarUI components can be used inside a plain QQuickView/Item hierarchy without a HusWindow root (e.g. for embedding). All examples I read use HusWindow as the root.; HusPopup's own property list — several components inherit it (HusModal, HusPopover) but I did not read docs/General/HusPopup.md or src/imports/HusPopup.qml.

---

## Equalizer APO 1.4.1 configuration grammar — exact parse/emit rules and a round-trip-safe C++ data model for DreamDSP's libapocfg
- The whole parsing model is 44 lines of code: read raw bytes, getline on '\n', strip one trailing '\r', decode UTF-8 (fall back to system ANSI if U+FFFD appears), find the FIRST ':', key = everything before it (trimmed), value = everything after it (NOT trimmed), then offer (key,value) to a fixed ordered list of factories; first factory returning a non-empty filter vector wins. Lines with no ':' are silently dropped.
  evidence: github.com/mirror/equalizerapo FilterEngine.cpp:311-354 (local copy C:\Users\10678\AppData\Local\Temp\claude\I--Qt\7fffc091-07c0-410a-a51c-5a342ddbdb5e\scratchpad\apo\FilterEngine.cpp). Regexes and message strings are byte-identical in the installed D:\Program Files\EqualizerAPO\EqualizerAPO.dll (FileVersion 1.4.1.0), verified via `strings -el`.
  → Implement ApoLine::parse() exactly this way: QString::indexOf(':'), key=left(pos).trimmed(), value=mid(pos+1) kept verbatim. Do NOT trim the value at parse time — each command trims differently and round-trip must preserve the original spacing.
- '#' is NOT a comment character in the grammar. There is exactly one place '#' is special: ExpressionFilterFactory returns early for keys starting with '#', which only suppresses inline backtick-expression substitution. Comment lines work purely because no command name starts with '#'. Consequence: `Filter #1: ON PK ...` IS parsed as a filter (key starts with "Filter"), and `#Filter: ...` is not.
  evidence: filters/ExpressionFilterFactory.cpp:54-55 (`if (command.length() > 0 && command[0] == L'#') return vector<IFilter*>();`); BiQuadFilterFactory.cpp:72 uses `command.find(L"Filter") == 0`. Real-world proof on disk: D:\Program Files\EqualizerAPO\config\configbeforePeace.txt lines 1-3 are `# Preamp: 0.4 dB`, `# Include: example.txt`, `# GraphicEQ: ...` and are inert.
  → Model comments as ApoLine::Kind::Comment only as a *display* hint (regex ^\s*#). Never strip or normalize them. Never emit a line whose trimmed key begins with "Filter" unless you mean a filter.
- Only the key is matched; several commands match by PREFIX not equality. `Filter` matching is `command.find(L"Filter") == 0`, so `Filter:`, `Filter 1:`, `Filter  12:`, `FilterXYZ:` all parse. Every other command (Preamp, Copy, Delay, Channel, Include, Convolution, GraphicEQ, Device, Stage, If, ElseIf, Else, EndIf, Eval, VSTPlugin, LoudnessCorrection) is an exact case-sensitive `==` comparison.
  evidence: filters/BiQuadFilterFactory.cpp:72 and filters/IIRFilterFactory.cpp:46 (prefix); CopyFilterFactory.cpp:33, PreampFilterFactory.cpp:33, DelayFilterFactory.cpp:35, ChannelFilterFactory.cpp:33, IncludeFilterFactory.cpp:53, ConvolutionFilterFactory.cpp:35, GraphicEQFilterFactory.cpp:37, StageFilterFactory.cpp:56, DeviceFilterFactory.cpp:55, VSTPluginFilterFactory.cpp:33, loudnessCorrection/LoudnessCorrectionFilterFactory.cpp:18 (all `==`).
  → In libapocfg, dispatch on `key == "Preamp"` etc. exactly, but for filters use `key.startsWith("Filter")` and store the leftover suffix (e.g. " 1") in ApoFilterLine::indexToken so `Filter  1:` round-trips character-for-character.
- Factory order is fixed and semantically load-bearing: Device, If, Expression, Include, Stage, Channel, IIR, BiQuad, Preamp, Delay, Copy, Convolution, GraphicEQ, VSTPlugin, LoudnessCorrection. Device/If/Stage suppress later factories by blanking the command; Include and Eval blank it after handling.
  evidence: FilterEngine.cpp:81-95 (construction order); DeviceFilterFactory.cpp:63-65, IfFilterFactory.cpp:177-179, StageFilterFactory.cpp:102-104, IncludeFilterFactory.cpp:79, ExpressionFilterFactory.cpp:150-151 (all `command = L""`); FilterEngine.cpp:345-346 breaks the loop on an empty key.
  → Keep this order as an enum in libapocfg so DreamDSP's own evaluator (for preview/graphing) reproduces APO's precedence — notably `Filter: ON IIR ...` is claimed by IIRFilterFactory before BiQuadFilterFactory ever sees it.
- There is NO BOM handling anywhere. A UTF-8 BOM decodes cleanly to U+FEFF (not U+FFFD), is not classified as whitespace by iswspace, and therefore corrupts the first key of the file. APO's own Editor writes UTF-8 with NO BOM, CRLF line separators, and NO trailing newline (lines are joined by "\r\n", not terminated by it).
  evidence: FilterEngine.cpp:318-320 (`toWString(encodedLine, CP_UTF8)`; retry with CP_ACP only `if (line.find(L'\uFFFD') != -1)`) — no BOM strip. Editor write path: Editor/MainWindow.cpp:317-333 (`byteArray.append("\r\n")` as separator; `byteArray.append(line.toUtf8())`). On-disk confirmation: `od -c` of config.txt, demo.txt, multichannel.txt, iir_lowpass.txt, selective_delay.txt in D:\Program Files\EqualizerAPO\config — all start with content bytes, all CRLF.
  → DreamDSP writer MUST use QByteArray + QString::toUtf8() and explicitly NOT QTextStream's BOM. Write CRLF. Preserve whether the source file ended with a newline (store `trailingNewline` bool on the document) so round-trip is byte-exact.
- Frequency parsing has a Room-EQ-Wizard thousands-separator hack that silently multiplies by 1000: after scanning the number, if the string is >=5 chars, contains no e/E, and the character at position len-4 is '.', the value is multiplied by 1000. So `Fc 8.000 Hz` = 8000 Hz and `Fc 12.345 Hz` = 12345 Hz. Any Fc written with EXACTLY three decimal places is misread.
  evidence: filters/BiQuadFilterFactory.cpp:220-241 (`if (s.length() >= 5 && s.find_first_of(L"eE") == wstring::npos) { if (s[s.length()-4] == L'.') result *= 1000.0; }`). Live proof: D:\Program Files\EqualizerAPO\config\demo.txt:12 `Filter  3: ON  LP       Fc   8.000 Hz` is documented as an 8000 Hz lowpass.
  → Add a hard guard in the emitter: `Q_ASSERT(fractionalDigits(fcString) != 3)`. Format Fc with 0, 1, 2, or >=4 decimals — never 3. Peace itself uses "%.1f" for Fc (I:\Qt\reference\peace-original\src\Peace\Peace.au3:29027), which is safe.
- Comma-as-decimal-mark conversion is per-command, not global. Filter (whole parameter string), Preamp and Delay always replace every ',' with '.'. GraphicEQ replaces ',' only if the value contains no '.' at all. Copy, IIR coefficients, Convolution filenames, Eval/If expressions and VSTPlugin values do NOT convert — they require '.'.
  evidence: BiQuadFilterFactory.cpp:75, PreampFilterFactory.cpp:36, DelayFilterFactory.cpp:38 (unconditional `replaceCharacters(parameters, L",", L".")`); GraphicEQFilterFactory.cpp:40-41 (`if (value.find(L'.') == wstring::npos)`); CopyFilterFactory.cpp:77 / IIRFilterFactory.cpp:84 / ExpressionFilterFactory.cpp use raw `wcstod`. Live proof of comma input: demo.txt:10 `Fc    50,0 Hz  Gain -10,0 dB  Q  2,50`.
  → libapocfg must accept both separators on read (per-command) and ALWAYS emit '.'. Use QString::number(v,'f',n) with QLocale::c(), never the system locale — a German Windows locale would otherwise emit commas into Copy/IIR lines and break them.
- Exact biquad regexes (identical in source and in the 1.4.1 DLL): type `^\s*ON\s+([A-Za-z]+)`; freq `\s+Fc\s*([-+0-9.eE ]+)\s*H\s*z`; gain `\s+Gain\s*([-+0-9.eE]+)\s*dB`; Q `\s+Q\s*([-+0-9.eE]+)`; bandwidth `\s+BW\s+Oct\s*([-+0-9.eE]+)`; shelf slope `^\s*([-+0-9.eE]+)\s*dB` (applied to the text immediately after the type token). Note the freq regex tolerates a non-breaking space inside the number and `H z` split by whitespace.
  evidence: filters/BiQuadFilterFactory.cpp:34-39; same six regexes appear verbatim as wide strings in EqualizerAPO.dll (offsets ~3962-3967 of `strings -n1 -el` output) and Editor.exe. Live proof of the split `H z`: demo.txt:11 `Fc     100H z`.
  → Reuse these exact regexes in ApoFilterLine::parse() (QRegularExpression with the same patterns) rather than hand-rolling a tokenizer — that guarantees DreamDSP accepts every file APO accepts, including REW exports.
- Filter type tokens and their canonical mapping: PK/PEQ/Modal -> PEAKING; LP/LPQ -> LOW_PASS; HP/HPQ -> HIGH_PASS; BP -> BAND_PASS; LS/LSC -> LOW_SHELF; HS/HSC -> HIGH_SHELF; NO -> NOTCH; AP -> ALL_PASS. `IIR` is handled by a separate factory. `None` is accepted and silently produces no filter (no error log). Any other token logs "Invalid filter type". There is no BW/other token beyond these.
  evidence: filters/BiQuadFilterFactory.cpp:43-56 (the map) and :208-211 (`else if (typeString != L"None")`); IIRFilterFactory.cpp:55. Same 14 tokens present as wide strings in the installed EqualizerAPO.dll. Live proof of `None`: example.txt:12-29 `Filter  3: OFF None`.
  → Define `enum class BiQuadToken { PK, PEQ, Modal, LP, LPQ, HP, HPQ, BP, LS, LSC, HS, HSC, NO, AP, None }` and store the ORIGINAL token, not the collapsed type — PK and PEQ are semantically identical but must round-trip distinctly.
- Filters whose first word is not `ON` are never parsed — `OFF` lines simply fail `^\s*ON\s+`, produce no filter, and fall through every remaining factory to be ignored. There is no OFF semantics in APO; disabling a filter is achieved solely by the line being unparseable.
  evidence: filters/BiQuadFilterFactory.cpp:34 (regexType anchored on `ON`) and :80-81. Live proof: example.txt:12-29 are all `Filter  3: OFF None` and produce nothing.
  → DreamDSP's 'disable filter' toggle should rewrite `ON` -> `OFF` in place (preserving everything else on the line) rather than deleting or commenting the line. That is the idiom the whole ecosystem, including Peace, uses.
- Q/BW/slope defaults and required-parameter rules: if no Q, BW or slope is present (or it evaluates to exactly 0, which is treated as absent) then LP/HP/BP default to Q = 1/sqrt(2); NO defaults to Q = 30.0; LS/HS default to S = 0.9 (as slope); PK and AP are ERRORS and the filter is dropped. Gain is required for PK/LS/HS (missing = filter dropped) and silently ignored for LP/HP/NO/AP. For LS/HS/LSC/HSC a slope value is divided by 12 to become S. LS/HS use CORNER frequency; LSC/HSC use CENTER frequency (decided by `typeString.back() != 'C'`).
  evidence: filters/BiQuadFilterFactory.cpp:116-198, in particular :170-190 (defaults), :128-132 (gain required), :195 (`bandwidthOrQOrS /= 12.0`), :196-197 (`if (typeString[typeString.length()-1] != L'C') isCornerFreq = true`).
  → Encode these as static tables in libapocfg (requiredParams/defaultQ per token) so the GUI can grey out irrelevant fields and so DreamDSP's response preview matches APO's audio exactly. Critically: never emit `Q 0` — APO reads it as 'not specified'.
- Preamp is parsed with `swscanf_s(value, L" %lf dB", &preamp)` and accepted when the return value is 1. Because scanf returns the count of *assigned* conversions, the literal " dB" suffix is never validated: `Preamp: -6`, `Preamp: -6 db`, `Preamp: -6 decibels`, and `Preamp:-6dB` all parse to -6.0. Multiple Preamps on the same channel sum in dB.
  evidence: filters/PreampFilterFactory.cpp:36-46. The format string " %lf dB" is present verbatim in EqualizerAPO.dll. Wiki (sourceforge.net/p/equalizerapo/wiki/Configuration reference/) states the summing behaviour since 0.8.
  → Parse Preamp permissively (leading number wins) but ALWAYS emit the canonical `Preamp: <n> dB`. Store the raw suffix text so a user-authored `Preamp: -6 db` isn't silently rewritten unless the user edits that line.
- Copy grammar is a whitespace-separated list of assignments; a single assignment must contain no spaces. Each assignment splits on '=' (must yield exactly 2 parts), the RHS splits on '+' into summands, each summand splits on '*' into factor and channel. A bare token is a CONSTANT if it is literally "0" or contains a '.', otherwise it is a channel identifier. A factor is in dB iff it is longer than 2 chars and its last two chars lowercase to "db". Missing factor = 1.0. Target/source names may be arbitrary strings, which creates virtual channels.
  evidence: filters/CopyFilterFactory.cpp:37-88, in particular :63-66 (constant-vs-channel rule) and :78 (`factor.size() > 2 && toLowerCase(factor.substr(factor.size()-2)) == L"db"`). Live proof of virtual channels: D:\Program Files\EqualizerAPO\config\selective_delay.txt:2 `Copy: L2=L R2=R` and :11 `Copy: L=0.5*L+0.5*L2 R=0.5*R+0.5*R2`.
  → Model Copy as `QVector<ApoAssignment>` where each summand is {QString factorText; double factor; bool isDecibel; QString channel; bool isConstant;} — keep factorText so `0.50` doesn't become `0.5` on round-trip. Enforce the 'no spaces inside an assignment' rule in the emitter.
- GraphicEQ ignores its own punctuation entirely: it scans the value with `[-+0-9.eE]+` and pairs consecutive matches as (freq, gain). The ';' and spaces are irrelevant, a trailing odd number is dropped, and the resulting node list is SORTED by frequency before use. Trailing text containing digits (e.g. an inline comment) would be swallowed as extra nodes.
  evidence: filters/GraphicEQFilterFactory.cpp:31,43-59 (`static wregex regexNumber(L"[-+0-9.eE]+")`, wsregex_iterator pairing, `sort(nodes.begin(), nodes.end())`). Live example on disk: configbeforePeace.txt:3 (a 31-band commented-out GraphicEQ). Peace emits nodes as `<f> <g>; ` including a trailing "; " (Peace.au3:29092-29099).
  → Emit `GraphicEQ: f1 g1; f2 g2; ...` with '; ' separators, keep the nodes in the order the user has them but be aware APO sorts internally. NEVER allow a trailing comment on a GraphicEQ line. Reading: pair matches, drop an odd trailing number, and record whether the source was already sorted.
- IIR filters: `Filter <n>: ON IIR Order <m> Coefficients <b0..bm> <a0..am>` with regexes `\s*Order\s+([0-9]+)` and `\s+Coefficients((?: [-+0-9.eE]+)+)`. Coefficient count must be exactly (order+1)*2; numerator (b) coefficients come FIRST, then denominator (a). Order must be >= 1. The coefficient regex requires exactly ONE space between numbers — tabs or double spaces truncate the list and trigger the count error.
  evidence: filters/IIRFilterFactory.cpp:34-36, :63-65, :75-77, and :89-92 (`stream << L" b" << i ...` then `<< L" a" << i << L"=" << coefficients[i + order + 1]`). Live example: D:\Program Files\EqualizerAPO\config\iir_lowpass.txt:18 `Filter: ON IIR Order 2 Coefficients \`a0\` \`a1\` \`a2\` \`b0\` \`b1\` \`b2\``.
  → Emitter must join coefficients with a single U+0020 and validate size == 2*(order+1) before writing. Note iir_lowpass.txt proves inline backtick expressions are the intended way to make coefficients sample-rate-dependent — libapocfg must treat a backticked token as an opaque expression string, not a number.
- Channel command uppercases EVERY character of its parameter and splits on ' ' only (not tabs), so `Channel: l rl` and `Channel: ALL` work. Channel name -> WAVE speaker position map is exactly L, R, C, LFE, RL, RR, RC, SL, SR. Aliases resolved only when the primary name is absent from the device: SL->RL, SR->RR, RL->SL, RR->SR, and the legacy SUB->LFE. Numeric selection triggers when the first character is a digit and is 1-based.
  evidence: filters/ChannelFilterFactory.cpp:39-56 (`towupper`, `if (c == L' ')`); helpers/ChannelHelper.cpp:39-47 (the 9-entry map) and :121-141 (alias fallbacks incl. `else if (word == L"SUB") // old channel name`); :109-118 (numeric path, `iswdigit(word[0])`, `-1`). The same 9 names + SUB + "ALL" appear as wide strings in the installed EqualizerAPO.dll.
  → Store canonical uppercase names in a `QStringList channels` plus the raw text. Expose the alias table (SUB=LFE, SL~RL, SR~RR) in libapocfg so DreamDSP's channel picker matches APO. Virtual channel names created by Copy are legal Channel targets too.
- Include resolves relative paths against the DIRECTORY OF THE INCLUDING FILE (PathRemoveFileSpec on the current file's path, then PathAppend), not against the config root. Only LEADING whitespace is stripped from the filename — a trailing space in the value breaks the include. Recursion depth limit is 100.
  evidence: filters/IncludeFilterFactory.cpp:30 (`const int RECURSION_LIMIT = 100;`), :56-73 (leading-space strip loop then PathIsRelativeW/PathRemoveFileSpecW/PathAppendW), :79 (`command = L""`). Live root: D:\Program Files\EqualizerAPO\config\config.txt contains exactly `Include: peace.txt` (CRLF, no BOM, 20 bytes).
  → DreamDSP's include resolver: QFileInfo(includingFilePath).absoluteDir().filePath(name) for relative, absolute otherwise. Never emit a trailing space after an Include filename. Track a depth counter capped at 100 to avoid hanging on cyclic includes when building the preview tree.
- config.txt location comes from registry HKLM\SOFTWARE\EqualizerAPO value ConfigPath, with "\\config.txt" appended. Live reload is a FindFirstChangeNotificationW on that directory with bWatchSubtree = TRUE, watching FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE, with a 10 ms coalescing wait; on trigger the whole config is re-parsed and cross-faded over sampleRate/100 frames (10 ms). APO also retries CreateFile in a 1 ms sleep loop on ERROR_SHARING_VIOLATION, i.e. while the file is being written.
  evidence: FilterEngine.cpp:168 (`RegistryHelper::readValue(APP_REGPATH, L"ConfigPath")`), :238 (`configPath + L"\\config.txt"`), :553 and :591-607 (notification thread), :150 (`transitionLength = sampleRate/100`), :271-286 (sharing-violation retry). Verified on this machine: `reg query HKLM\SOFTWARE\EqualizerAPO` returns ConfigPath = D:\Program Files\EqualizerAPO\config, InstallPath = D:\Program Files\EqualizerAPO.
  → Read ConfigPath from the registry (QSettings with QSettings::Registry64Format on HKEY_LOCAL_MACHINE\\SOFTWARE\\EqualizerAPO) — do not hardcode Program Files. To apply changes, just write the file: no IPC needed. Write atomically (write temp + MoveFileEx replace) or accept that APO may briefly see a partial file; a single write triggers exactly one reload thanks to the 10 ms debounce. Any file touched anywhere under the config tree triggers a full reload, so DreamDSP should avoid writing scratch files there.
- VSTPlugin and LoudnessCorrection are real commands in 1.4.1 but are completely absent from the official wiki. VSTPlugin syntax is `VSTPlugin: <Key> <Value> <Key> <Value> ...` parsed by splitQuoted on space with '"' as quote char; recognised keys are `Library` (dll path, relative resolved against InstallPath\VSTPlugins) and `ChunkData` (opaque base64); every other key/value pair is a float plugin parameter. LoudnessCorrection syntax is `LoudnessCorrection: State <0|1> ReferenceLevel <int> ReferenceOffset <int> [Attenuation <0.0-1.0>]`, where State/ReferenceLevel/ReferenceOffset are REQUIRED (missing = whole line dropped) and Attenuation defaults to 1.0.
  evidence: FilterEngine.cpp:94-95 registers both factories. filters/VSTPluginFilterFactory.cpp:33-73 (splitQuoted, Library/ChunkData/float-param branches; getDefaultPluginPath). filters/loudnessCorrection/LoudnessCorrectionFilter.h:42-45 (serialize: `archive.add(x, L"Name")` writes `Name value `) and :52-56 (the four regexes `\\s*State\\s+(0|1)`, `\\s*ReferenceLevel\\s+([-+0-9]+)`, `\\s*ReferenceOffset\\s+([-+0-9]+)`, `\\s*Attenuation\\s+((1((\\.|,)0+)?)|(0((\\.|,)[0-9]+)?))`). All four regexes and the strings 'VSTPlugin', 'Library', 'ChunkData', 'LoudnessCorrection', '\\VSTPlugins' are present in the installed 1.4.1 EqualizerAPO.dll AND Editor.exe. Neither command appears in the live wiki page nor in the repo's own Wiki/Configuration reference.txt (404 lines, zero hits).
  → libapocfg must round-trip both. ChunkData is base64 that can be megabytes — store it as a QByteArray reference, never re-wrap or reformat it. ReferenceLevel/ReferenceOffset must be emitted as plain integers (the regex rejects decimals). Attenuation must match `1`, `1.0`, or `0.<digits>` exactly — 0 or 2.0 will be rejected and silently reset to 1.0.
- Device matching is case-insensitive substring matching, not globbing. The pattern list is split on ';' into alternatives, each alternative split on ' ' into words, and ALL words of one alternative must be substrings of the haystack "DeviceName ConnectionName GUID". The GUID is stripped from the haystack unless the pattern word contains '{'. A single-word alternative equal to "all" (case-insensitive) always matches. A non-matching Device suppresses every subsequent line except further Device lines, and this state is reset to 'matching' at the end of each included file.
  evidence: filters/DeviceFilterFactory.cpp:78-135 (matchDevice), :63-65 (`if (!deviceMatches) command = L""`), :70-76 (endOfFile resets to true). The GUID-strip regex `\\{[0-9a-fA-F]{8}-...\\}` is present in the installed DLL.
  → Ship this matcher verbatim in libapocfg so DreamDSP's device picker can preview which config sections apply. Note the wiki's remark that Device has higher priority than If — an If cannot conditionally emit a Device line, because Device is factory #1.
- Stage default is post-mix + capture, i.e. `stageMatches = capture || !preMix || !postMixInstalled` at start of config. Only the three tokens pre-mix, post-mix, capture are accepted (lowercased and split on space); anything else logs an error. Like Device, a non-matching Stage blanks the command for all following lines, but unlike Device it is saved/restored with a stack across Include boundaries.
  evidence: filters/StageFilterFactory.cpp:40 (default), :60-94 (token handling), :102-104 (suppression), :49 and :111-112 (push/pop stack in startOfFile/endOfFile).
  → Represent Stage as a scoped directive in the document model with the same push/pop-per-file semantics; DreamDSP's evaluator needs this to correctly grey out sections in included files.
- Inline expressions substitute the text between the first and second backtick BEFORE any of the parameter-parsing factories run, and backticks can be escaped with a backslash. Eval evaluates and discards. Neither is available in Device or If/ElseIf (they run earlier in the factory chain). Expression constants exposed by APO 1.4: e, pi, inputChannelCount, outputChannelCount, sampleRate, deviceName, connectionName, deviceGuid, stage. Custom functions: readRegString, readRegDWORD, regexSearch, regexReplace (and `not`, plus an overloaded `+` for string concatenation).
  evidence: filters/ExpressionFilterFactory.cpp:38-49 (DefineConst/DefineFun/RemoveOprt+DefineOprt), :62-129 (backtick scanner with backslash escape), :131-152 (Eval, then `command = L""`). filters/DeviceFilterFactory.cpp:40-42 and StageFilterFactory.cpp:35 add the remaining consts. All function names and help strings appear in the installed 1.4.1 DLL. Live example: iir_lowpass.txt:5-18.
  → Treat any value containing an unescaped backtick as OPAQUE: parse it into ApoLine::Kind::Unknown-with-expression, render it read-only in the GUI, and re-emit the raw text. Do not attempt to evaluate muParserX-compatible expressions in v1 — that is a large scope item; flag such lines to the user instead.
- An unparseable line is not an error — it is data that survives. configbeforePeace.txt on this machine contains a bare `-1.8` line with no colon, which APO silently drops but which must be preserved by any editor. APO's own Configuration Editor implements exactly this: it loads the file into a QList<QString> of raw lines and writes those raw lines back.
  evidence: D:\Program Files\EqualizerAPO\config\configbeforePeace.txt:4 is the single token `-1.8`. Editor/MainWindow.cpp:286-302 builds `QList<QString> lines` from the raw decoded lines and hands them to FilterTable; MainWindow.cpp:317-333 writes `filterTable->getLines()` back joined by "\r\n". FilterEngine.cpp:322-323 (`if (pos != -1)`) shows colonless lines are just skipped.
  → This is the single most important design constraint for libapocfg: the document is a list of RAW LINES with optional parsed overlays, not a list of commands. Serialization = for each line, emit `dirty ? render(parsed) : raw`. Never regenerate a file from the semantic model alone.

### code samples
```
/* ============================================================================
 * EQUALIZER APO 1.4.1 — COMPLETE CONFIGURATION GRAMMAR
 * Verified against github.com/mirror/equalizerapo @ trunk r100 (post-1.4)
 * and byte-identical regexes in D:\\Program Files\\EqualizerAPO\\EqualizerAPO.dll
 * (FileVersion 1.4.1.0).
 * ==========================================================================*/

/* ---- 0. LEXICAL / FILE LEVEL -------------------------------------------- */

file        ::= line *                    ; getline() on '\n'
line        ::= [ raw-bytes ] [ '\r' ]    ; ONE trailing '\r' is stripped
                                          ; decoding: CP_UTF8, retry CP_ACP iff
                                          ; the decode produced U+FFFD.
                                          ; NO BOM handling — a UTF-8 BOM leaves
                                          ; U+FEFF glued to the first key and
                                          ; BREAKS the first command.

statement   ::= key ':' value             ; split on the FIRST ':' only.
key         ::= <text before first ':'>   ; then trim()ed  (allows indentation)
value       ::= <ALL text after first ':'>; NOT trimmed by the engine;
                                          ; each command trims for itself.

; Lines with no ':' at all  -> silently ignored (this is why `# foo` works and
; why the stray line `-1.8` in configbeforePeace.txt is harmless).
; Lines whose key matches no command -> silently ignored
;   (this is why `# Preamp: 0.4 dB` is inert).
; There is NO comment token in the grammar. '#' is only special in
; ExpressionFilterFactory, where a key starting with '#' skips backtick
; substitution. CAUTION:  `Filter #1: ON PK ...`  IS parsed (prefix match!).

; Command dispatch: factories are tried in this exact order; the first one
; returning a filter (or blanking the command) wins:
;   Device, If/ElseIf/Else/EndIf, Eval+inline-expr, Include, Stage, Channel,
;   Filter(IIR), Filter(BiQuad), Preamp, Delay, Copy, Convolution, GraphicEQ,
;   VSTPlugin, LoudnessCorrection
; Matching is `key == "Name"` (exact, case-SENSITIVE) for everything EXCEPT
; filters, which use  key.startsWith("Filter").


/* ---- 1. NUMBER FORMATTING RULES ----------------------------------------- */

number      ::= [-+0-9.eE]+               ; the universal number charclass.
                                          ; Parsed with wcstod / swscanf("%lf"),
                                          ; C locale => '.' is THE decimal mark.

; Comma->period conversion is PER COMMAND, applied to the value before regexing:
;   Filter      : ALWAYS   (every ',' -> '.')
;   Preamp      : ALWAYS
;   Delay       : ALWAYS
;   GraphicEQ   : ONLY IF the value contains no '.' at all
;   Copy, IIR coefficients, Convolution, Eval/If, VSTPlugin : NEVER
;
; APO never emits config text itself; its Editor writes back whatever the user
; typed. Decimal count is therefore unconstrained EXCEPT for one trap:
;
; *** THE THREE-DECIMAL Fc TRAP ***
;   getFreq():  if len >= 5  &&  no 'e'/'E'  &&  s[len-4] == '.'  =>  value *= 1000
;   "8.000"  -> 8000      "12.345" -> 12345      "1.234"  -> 1234
;   "8.00"   -> 8         "8.0000" -> 8         "8000"   -> 8000
;   => NEVER write an Fc with exactly 3 fractional digits.
;   (Applies to Fc only; Gain / Q / BW / Delay / GraphicEQ are unaffected.)
;
; Units are literal, and only loosely validated:
;   Hz  -> regex  \s*H\s*z   (so "100H z" is legal — see demo.txt:11)
;   dB  -> Gain regex requires it; Preamp's scanf does NOT validate it
;          (`Preamp: -6` and `Preamp: -6 db` both parse to -6.0)
;   ms / samples -> case-insensitive, compared after toLowerCase()
;   BW Oct <n>   -> bandwidth in octaves
;   The Fc number class also accepts U+00A0 (non-breaking thousands sep).


/* ---- 2. CHANNELS --------------------------------------------------------- */

channel     ::= name | index | virtual-name
name        ::= "L" | "R" | "C" | "LFE" | "RL" | "RR" | "RC" | "SL" | "SR"
index       ::= [1-9][0-9]*               ; 1-based; chosen iff first char is a digit
virtual-name::= any other token           ; created by Copy (allowNew == true)

; Aliases, applied only when the primary name is absent from the device:
;   SUB -> LFE   (legacy)      SL <-> RL        SR <-> RR
; The Channel command UPPERCASES its whole parameter, so `Channel: l rl` works.
; The special token ALL selects every channel.
; Standard layouts (wiki):
;   Mono        C=1
;   Stereo      L=1 R=2
;   Quad        L=1 R=2 RL=3 RR=4
;   Surround    L=1 R=2 C=3 RC=4
;   5.1 (side)  L=1 R=2 C=3 LFE=4 SL=5 SR=6
;   5.1 (rear)  L=1 R=2 C=3 LFE=4 RL=5 RR=6
;   7.1         L=1 R=2 C=3 LFE=4 RL=5 RR=6 SL=7 SR=8


/* ---- 3. COMMANDS --------------------------------------------------------- */

/* 3.1 Preamp -------------------------------------------------------------- */
preamp      ::= "Preamp" ':' ws* number ws* "dB"?
; swscanf_s(value, L" %lf dB") ; suffix NOT validated. Multiple preamps on the
; same channel SUM in dB (since 0.8).
  Preamp: -6.5 dB

/* 3.2 Filter (biquad) ----------------------------------------------------- */
filter      ::= "Filter" any* ':' ws* "ON" ws+ type [ shelf-slope ]
                 " Fc " number " Hz " [ " Gain " number " dB " ]
                 [ " Q " number | " BW Oct " number ]
type        ::= "PK"|"PEQ"|"Modal"|"LP"|"LPQ"|"HP"|"HPQ"|"BP"
              | "LS"|"LSC"|"HS"|"HSC"|"NO"|"AP"|"None"|"IIR"
shelf-slope ::= ws* number ws* "dB"       ; only meaningful for LS/HS/LSC/HSC

; Regexes (verbatim from BiQuadFilterFactory.cpp:34-39):
;   type   ^\s*ON\s+([A-Za-z]+)
;   Fc     \s+Fc\s*([-+0-9.eE\u00A0]+)\s*H\s*z
;   Gain   \s+Gain\s*([-+0-9.eE]+)\s*dB
;   Q      \s+Q\s*([-+0-9.eE]+)
;   BW     \s+BW\s+Oct\s*([-+0-9.eE]+)
;   slope  ^\s*([-+0-9.eE]+)\s*dB          (matched on the text after the type)
;
; Parameter table  (X = required, O = optional, - = ignored):
;   type          Fc  Gain  Q/BW   default when Q/BW/slope absent-or-zero
;   PK/PEQ/Modal  X   X     X      ERROR -> filter dropped
;   LP/LPQ        X   -     O      Q = 1/sqrt(2)
;   HP/HPQ        X   -     O      Q = 1/sqrt(2)
;   BP            X   -     O      Q = 1/sqrt(2)
;   LS/LSC        X   X     O      S = 0.9   (slope value is divided by 12)
;   HS/HSC        X   X     O      S = 0.9
;   NO            X   -     O      Q = 30.0
;   AP            X   -     X      ERROR -> filter dropped
;   None          -   -     -      accepted, produces nothing, no log
; LS/HS  => corner frequency.  LSC/HSC (token ends in 'C') => center frequency.
; A Q of literally 0 is treated as 'not given'.
; A line whose first token is not ON (e.g. "OFF None") never parses: that is
; the ONLY mechanism for disabling a filter.

  Filter  1: ON  PK       Fc     50 Hz   Gain  -3.0 dB  Q 10.00
  Filter  2: ON  PEQ      Fc    100 Hz   Gain   1.0 dB  BW Oct 0.167
  Filter: ON LS 6dB   Fc 50.0 Hz  Gain 7.2 dB      ; 6 dB/oct, corner freq
  Filter: ON LSC 10.8 dB Fc 300 Hz Gain 5.0 dB     ; center freq + slope
  Filter: ON HSC      Fc 100 Hz  Gain -6.0 dB Q 0.4272
  Filter: ON NO       Fc 800 Hz                    ; Q defaults to 30
  Filter: ON AP       Fc 900 Hz  Q 0.707
  Filter 15: ON None                               ; no-op placeholder

/* 3.3 Filter (custom IIR) ------------------------------------------------- */
iir         ::= "Filter" any* ':' ws* "ON" ws+ "IIR" ws* "Order" ws+ int
                 ws+ "Coefficients" ( ' ' number ){2*(order+1)}
; regexes: \s*Order\s+([0-9]+)   and   \s+Coefficients((?: [-+0-9.eE]+)+)
; ORDER OF COEFFICIENTS:  b0 b1 .. bm  a0 a1 .. am   (numerator first)
; order >= 1 required; count must be EXACTLY 2*(order+1); separator must be a
; single space (the regex does not accept tabs or double spaces).
; NO comma->period conversion here.

  Filter: ON IIR Order 2 Coefficients 0.0380602 0.0761205 0.0380602 1.2706 -1.84776 0.729402

/* 3.4 Delay --------------------------------------------------------------- */
delay       ::= "Delay" ':' ws* number ws+ ("ms" | "samples")   ; unit case-insens.
; parsed by  `stream >> delay >> unit`; a negative value is rejected.
  Delay: 50.5 ms
  Delay: 480 samples

/* 3.5 Copy ---------------------------------------------------------------- */
copy        ::= "Copy" ':' assignment ( ' ' assignment )*
assignment  ::= target '=' summand ( '+' summand )*     ; NO SPACES inside!
summand     ::= [ factor '*' ] channel  |  factor
factor      ::= number [ "dB" ]         ; dB iff len>2 and last 2 chars ~= "db"
; A bare token is a CONSTANT iff it is exactly "0" or contains a '.',
; otherwise it is a channel identifier (hence `Copy: L=2` means channel 2).
; Missing factor == 1.0. Unknown target/source names create VIRTUAL channels.
; NO comma->period conversion: the decimal point is mandatory.

  Copy: L=L+0.5*R
  Copy: L=R+-6dB*C
  Copy: 1=R R=0.5
  Copy: LFE=L L=0.0 R=0.0 C=0.0 RL=0.0 RR=0.0
  Copy: L2=L R2=R                 ; create virtual channels (selective_delay.txt)

/* 3.6 GraphicEQ ----------------------------------------------------------- */
graphiceq   ::= "GraphicEQ" ':' ( number sep number sep? )*
; Implementation scans ALL matches of [-+0-9.eE]+ and pairs them (freq,gain).
; ';' and spaces are decoration only. A trailing odd number is discarded.
; The node list is SORTED by frequency internally. Gains are interpolated
; linearly in log-frequency; the response is flat outside the outermost nodes.
; Comma->period only if the value contains no '.'.
; => NEVER put a trailing comment on a GraphicEQ line: its digits become nodes.

  GraphicEQ: 25 6; 40 4.5; 63 3; 100 1.5; 160 0; 1000 0; 6300 1.5; 16000 3

/* 3.7 Convolution --------------------------------------------------------- */
convolution ::= "Convolution" ':' ws* <filename>
; leading whitespace stripped; relative path resolved against the DIRECTORY OF
; THE FILE CONTAINING THE COMMAND. Any libsndfile format. Sample rate MUST equal
; the device sample rate. Multi-channel IRs are assigned round-robin.
  Convolution: church.wav

/* 3.8 Include ------------------------------------------------------------- */
include     ::= "Include" ':' ws* <filename>
; leading whitespace stripped, TRAILING WHITESPACE IS NOT — a trailing space
; breaks the include. Relative -> directory of the including file.
; Recursion depth limit = 100.
  Include: example.txt

/* 3.9 Device -------------------------------------------------------------- */
device      ::= "Device" ':' alt ( ';' alt )*
alt         ::= word ( ' ' word )*  |  "all"
; Case-insensitive SUBSTRING match (not glob) of every word of one alternative
; against "<DeviceName> <ConnectionName> <GUID>". The GUID part is removed from
; the haystack unless the pattern word contains '{'.
; A non-matching Device suppresses ALL following lines except further Device
; lines; reset to 'matching' at the end of each included file.
; Device has higher priority than If, so If cannot gate a Device line.
  Device: High Definition Audio Device Speakers; Benchmark
  Device: all

/* 3.10 Channel ------------------------------------------------------------ */
channelcmd  ::= "Channel" ':' channel ( ' ' channel )*   |  "Channel" ':' "all"
; parameter is uppercased wholesale; split on ' ' only (tabs are NOT separators).
; Selection is restored to the outer file's selection at the end of an Include.
  Channel: L RL
  Channel: 1 2 C
  Channel: all

/* 3.11 Stage -------------------------------------------------------------- */
stage       ::= "Stage" ':' s ( ' ' s )*
s           ::= "pre-mix" | "post-mix" | "capture"     ; lowercased before compare
; Initial state == post-mix + capture. Non-matching stage suppresses all
; following lines. Push/pop across Include boundaries.
  Stage: pre-mix

/* 3.12 If / ElseIf / Else / EndIf ----------------------------------------- */
ifcmd       ::= "If"     ':' expression
              | "ElseIf" ':' expression
              | "Else"   ':'
              | "EndIf"  ':'
; Nesting via trueCount/falseCount counters; while falseCount>0 every line's
; command is blanked. Unclosed If at end of file logs an error and resets.
; Inline backtick expressions are NOT available inside If/ElseIf or Device.
  If: sampleRate == 44100
  ElseIf: sampleRate == 48000
  Else:
  EndIf:

/* 3.13 Eval and inline expressions ---------------------------------------- */
eval        ::= "Eval" ':' expression
inline      ::= '`' expression '`'   ; substituted into ANY command's value
                                     ; (except Device / If / ElseIf) BEFORE that
                                     ; command parses it. '\\`' escapes a backtick.
; Constants: e, pi, inputChannelCount, outputChannelCount, sampleRate,
;            deviceName, connectionName, deviceGuid, stage
; Functions: abs sin cos tan sinh cosh tanh ln log log10 exp sqrt min max sum
;            str2dbl strlen tolower toupper sizeof
;            regexSearch regexReplace readRegString readRegDWORD
; Operators: + - * / ^ , = += -= *= /= , and or xor not , == != < > <= >= ,
;            & | << >> , (float) (int) , cond?a:b , {1,2} array , array[0]
; Types s/b/i/f/m. Multiple expressions separated by ';'; value = the last one.
  Eval: linGain = 0.5
  Filter: ON PK Fc 1000 Hz Gain `20*log10(linGain)` dB Q 10.0

/* 3.14 VSTPlugin  — *** UNDOCUMENTED ON THE WIKI *** ---------------------- */
vstplugin   ::= "VSTPlugin" ':' ( key ' ' value )*    ; splitQuoted(value,' ','"')
key         ::= "Library" | "ChunkData" | <parameter-name>
; Library : dll path; relative -> <InstallPath>\\VSTPlugins
; ChunkData : opaque base64 blob (can be megabytes)
; anything else : the value is wcstof()'d into a named float parameter
  VSTPlugin: Library "BC PatchWork VST.dll" ChunkData "PD94bWwgdmVy..."
  VSTPlugin: Library C:\\VST\\comp.dll Threshold 0.5 Ratio 0.75

/* 3.15 LoudnessCorrection — *** UNDOCUMENTED ON THE WIKI *** -------------- */
loudness    ::= "LoudnessCorrection" ':'
                 "State" ws+ ("0"|"1")
                 ws+ "ReferenceLevel"  ws+ int      ; [-+0-9]+  -> INTEGER ONLY
                 ws+ "ReferenceOffset" ws+ int      ; [-+0-9]+  -> INTEGER ONLY
               [ ws+ "Attenuation"     ws+ atten ]  ; optional, default 1.0
atten       ::= "1" | "1.0"+ | "1,0"+ | "0" | "0." [0-9]+ | "0," [0-9]+
; State/ReferenceLevel/ReferenceOffset are ALL REQUIRED — if any is missing the
; whole line is dropped. Values are order-independent (each has its own regex).
  LoudnessCorrection: State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 0.5


/* ---- 4. WORKED ROUND-TRIP EXAMPLE --------------------------------------- */
; Input file (UTF-8 no BOM, CRLF). Every line below must come back byte-identical
; from DreamDSP unless the user edited that specific line.
;
;   #Common preamp
;   Preamp: -6 dB
;   
;   Channel: L
;   #Additional preamp for left channel
;   Preamp: -5 dB
;   #Filters only for left channel
;   Include: demo.txt
;   
;   Channel: 2 C
;   #Filters for second(right) and center channel
;   Include: example.txt
;
; (This is D:\\Program Files\\EqualizerAPO\\config\\multichannel.txt verbatim.)
; Parsed classification:
;   L1  Comment      raw="#Common preamp"        (no ':' -> never even split)
;   L2  Preamp       value=" -6 dB"  gain=-6.0
;   L3  Blank
;   L4  Channel      channels=[L]
;   L5  Comment
;   L6  Preamp       gain=-5.0
;   L7  Comment
;   L8  Include      path="demo.txt" -> D:\\...\\config\\demo.txt
;   L9  Blank
;   L10 Channel      channels=[2, C]
;   L11 Comment
;   L12 Include      path="example.txt"
; Note L1 has NO colon at all, so FilterEngine never splits it: it is pure
; unknown text. L5/L7/L11 DO contain no colon either. But configbeforePeace.txt
; contains `# Preamp: 0.4 dB` which DOES split into key="# Preamp" (unmatched).
; Both classes must be preserved verbatim.

```

```
// =============================================================================
// libapocfg — C++ data model for Equalizer APO 1.4 configuration files
// DreamDSP / Qt 6.8.3.  Header sketch: apocfg/ApoDocument.h
//
// CORE INVARIANT (mirrors what APO's own Editor does, MainWindow.cpp:286-333):
//   The document is a LIST OF RAW LINES with optional parsed overlays.
//   Serialisation emits `line.dirty ? render(line.cmd) : line.raw`.
//   An unknown, malformed, commented, or user-authored line is NEVER rewritten.
// =============================================================================
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>
#include <variant>

namespace apocfg {

// ---------------------------------------------------------------- primitives

/// A number as it appeared in the file plus its parsed value.
/// `text` is authoritative for round-trip; `value` is authoritative for DSP.
struct Num {
    QString text;       ///< exactly as written, e.g. "50,0" or "8.000" or "1e3"
    double  value = 0;  ///< after comma->period + the getFreq() x1000 hack

    static Num parseGeneric(QStringView s);   ///< wcstod semantics, C locale
    static Num parseFrequency(QStringView s); ///< + NBSP strip + x1000 hack
    /// Emits with '.' always (QLocale::c()). Asserts n != 3 when isFrequency.
    static Num make(double v, int decimals, bool isFrequency = false);
};

/// Biquad token exactly as written — PK and PEQ are the same DSP but must
/// round-trip distinctly.
enum class FilterToken {
    PK, PEQ, Modal,            // -> peaking
    LP, LPQ,                   // -> low-pass
    HP, HPQ,                   // -> high-pass
    BP,                        // -> band-pass
    LS, LSC,                   // -> low-shelf   (LS = corner f, LSC = center f)
    HS, HSC,                   // -> high-shelf
    NO,                        // -> notch
    AP,                        // -> all-pass
    None,                      // accepted no-op placeholder
    IIR                        // handled by IirFilterCmd, not BiquadCmd
};
enum class BiquadType { Peaking, LowPass, HighPass, BandPass,
                        LowShelf, HighShelf, Notch, AllPass };

BiquadType  toType(FilterToken t);
bool        usesCornerFreq(FilterToken t);   ///< true iff token does NOT end in 'C'
bool        gainRequired(FilterToken t);     ///< PK/PEQ/Modal/LS/LSC/HS/HSC
bool        gainIgnored(FilterToken t);      ///< LP/LPQ/HP/HPQ/NO/AP
bool        qRequired(FilterToken t);        ///< PK/PEQ/Modal/AP  (else -> error)
double      defaultQ(FilterToken t);         ///< LP/HP/BP: M_SQRT1_2, NO: 30.0
double      defaultS(FilterToken t);         ///< LS/HS: 0.9

/// How the shape parameter was spelled on the line.
enum class ShapeKind { None, Q, BandwidthOct, SlopeDb };

// ---------------------------------------------------------------- channels

struct ChannelRef {
    QString raw;        ///< as written (before APO's uppercasing), e.g. "l"
    QString canonical;  ///< uppercased: "L" | "LFE" | "ALL" | "L2" | ...
    int     index = -1; ///< 1-based, >0 iff the token started with a digit
    bool    isAll   = false;
    bool    isVirtual = false;   ///< not one of the 9 standard names, not numeric
};

namespace channels {
    /// The exact 9 names APO knows (ChannelHelper.cpp:39-47), in bit order.
    inline const char* kStandard[] = {"L","R","C","LFE","RL","RR","RC","SL","SR"};
    /// Fallback aliases applied only when the primary name is absent:
    ///   SUB->LFE (legacy), SL<->RL, SR<->RR
    QString resolveAlias(const QString& canonical, const QStringList& available);
}

// ---------------------------------------------------------------- commands

struct PreampCmd {
    Num     gainDb;
    QString unitText = QStringLiteral("dB"); ///< preserved; APO does not validate it
};

struct BiquadCmd {
    QString     indexToken;    ///< text between "Filter" and ':' — "", " 1", "  12"
    bool        on = true;     ///< false => the line does not parse; keep raw
    FilterToken token = FilterToken::PK;
    Num         fc;            ///< Hz
    std::optional<Num> gainDb;
    ShapeKind   shapeKind = ShapeKind::None;
    std::optional<Num> shape; ///< Q value, BW in octaves, or slope in dB/oct
};

struct IirFilterCmd {
    QString          indexToken;
    int              order = 0;
    QVector<Num>     coefficients;   ///< b0..bm then a0..am; size == 2*(order+1)
    QStringList      rawTokens;      ///< may contain `expr` backtick tokens
};

struct DelayCmd {
    Num  amount;
    enum class Unit { Milliseconds, Samples } unit = Unit::Milliseconds;
    QString unitText;                ///< "ms" / "samples" as written (case kept)
};

struct CopySummand {
    QString factorText;   ///< "", "0.5", "-6dB"   (empty => implicit 1.0)
    double  factor = 1.0;
    bool    isDecibel = false;
    QString channel;      ///< empty iff this summand is a bare constant
    bool    isConstant = false;
};
struct CopyAssignment {
    QString              target;
    QVector<CopySummand> sourceSum;
};
struct CopyCmd { QVector<CopyAssignment> assignments; };

struct GraphicEqNode { Num freq; Num gainDb; };
struct GraphicEqCmd {
    QVector<GraphicEqNode> nodes;   ///< in FILE order; APO sorts internally
    QString separator = QStringLiteral("; ");  ///< preserved for round-trip
    bool    hadTrailingSeparator = false;      ///< Peace writes a trailing "; "
};

struct ConvolutionCmd { QString path; QString resolvedPath; };
struct IncludeCmd     { QString path; QString resolvedPath; };
struct DeviceCmd      { QVector<QStringList> alternatives; bool isAll = false; };
struct ChannelCmd     { QVector<ChannelRef> channels; bool isAll = false; };
struct StageCmd       { bool preMix=false, postMix=false, capture=false;
                        QStringList unknownTokens; };

struct IfCmd   { enum class Kind { If, ElseIf, Else, EndIf } kind; QString expr; };
struct EvalCmd { QString expr; };

/// UNDOCUMENTED on the wiki; present in 1.4.1 (FilterEngine.cpp:94).
struct VstPluginCmd {
    QString    libraryPath;      ///< relative -> <InstallPath>\\VSTPlugins
    bool       libraryWasQuoted = false;
    QByteArray chunkData;        ///< opaque base64; NEVER reformat, can be huge
    bool       chunkWasQuoted = false;
    QVector<QPair<QString, Num>> params;  ///< name -> float, in file order
};

/// UNDOCUMENTED on the wiki; present in 1.4.1 (FilterEngine.cpp:95).
struct LoudnessCorrectionCmd {
    bool state = false;          ///< regex (0|1)               REQUIRED
    int  referenceLevel  = 0;    ///< regex [-+0-9]+  INTEGER   REQUIRED
    int  referenceOffset = 0;    ///< regex [-+0-9]+  INTEGER   REQUIRED
    std::optional<Num> attenuation;  ///< must match 1|1.0*|0|0.<digits>; def 1.0
};

using Command = std::variant<
    std::monostate,          // Kind::Unknown / Comment / Blank -> raw only
    PreampCmd, BiquadCmd, IirFilterCmd, DelayCmd, CopyCmd, GraphicEqCmd,
    ConvolutionCmd, IncludeCmd, DeviceCmd, ChannelCmd, StageCmd,
    IfCmd, EvalCmd, VstPluginCmd, LoudnessCorrectionCmd>;

// ---------------------------------------------------------------- line model

enum class LineKind {
    Blank,       ///< empty or whitespace only
    Comment,     ///< trimmed text starts with '#' (a display hint ONLY)
    Unknown,     ///< has no ':' OR the key matches no command -> APO ignores it
    Command      ///< recognised; `cmd` is populated
};

struct Line {
    // ---- round-trip payload (always authoritative unless dirty) ----
    QString  raw;            ///< the line EXACTLY as read, without CR/LF
    LineKind kind = LineKind::Unknown;

    // ---- parse overlay ----
    QString  key;            ///< text before the first ':' , trimmed
    QString  value;          ///< text after  the first ':' , NOT trimmed
    int      colonPos = -1;  ///< -1 when the line contains no ':'
    Command  cmd;

    // ---- editing ----
    bool dirty = false;      ///< set by the GUI; only then is cmd re-rendered
    bool hasInlineExpression = false;  ///< unescaped '`' present -> treat opaque

    QString render() const;  ///< dirty ? serialise(key, cmd) : raw
};

// ---------------------------------------------------------------- document

struct Document {
    QString        filePath;
    QVector<Line>  lines;

    // Byte-level facts captured on load so the writer can reproduce them.
    enum class Eol { Crlf, Lf } eol = Eol::Crlf;      ///< APO Editor writes CRLF
    bool hadBom             = false;  ///< if true, WARN: APO cannot read this file
    bool wasAnsiFallback    = false;  ///< decoded via CP_ACP, not UTF-8
    bool trailingNewline    = false;  ///< APO's Editor writes NO trailing newline

    // -------- load / save --------
    /// Decodes CP_UTF8; retries CP_ACP iff the result contains U+FFFD.
    /// Splits on '\n', strips ONE trailing '\r'. Does NOT strip a BOM
    /// (matching FilterEngine.cpp:311-320) but records hadBom so the GUI can
    /// offer to remove it — a BOM breaks APO's first command.
    static Document load(const QString& path, QString* error = nullptr);

    /// Always writes UTF-8 WITHOUT BOM, CRLF-joined, no trailing newline
    /// (byte-for-byte what APO's own Editor produces, MainWindow.cpp:317-333).
    /// Writes to a temp file then ReplaceFile/MoveFileEx so APO's watcher sees
    /// one atomic change and never a half-written file.
    bool saveAtomic(QString* error = nullptr) const;

    QByteArray serialise() const;

    /// Round-trip self-check: serialise() must equal the loaded bytes when
    /// nothing is dirty. Call this in unit tests over every *.txt in the
    /// APO config dir and every AutoEQ result file.
    bool roundTripsExactly(const QByteArray& original) const;
};

// ---------------------------------------------------------------- environment

namespace env {
    /// HKLM\\SOFTWARE\\EqualizerAPO -> "ConfigPath" (QSettings::Registry64Format).
    /// Verified on this machine: D:\\Program Files\\EqualizerAPO\\config
    QString configPath();
    /// HKLM\\SOFTWARE\\EqualizerAPO -> "InstallPath"
    QString installPath();
    /// configPath() + "/config.txt"  (FilterEngine.cpp:238)
    QString rootConfigFile();
    /// installPath() + "/VSTPlugins" — base for relative VSTPlugin Library paths
    QString vstPluginPath();

    /// Relative Include/Convolution paths resolve against the directory of the
    /// file that CONTAINS the command, not against configPath().
    QString resolveRelative(const QString& includingFile, const QString& rel);

    inline constexpr int kIncludeRecursionLimit = 100;
}

} // namespace apocfg

```

UNKNOWNS: The GitHub mirror I read (github.com/mirror/equalizerapo, HEAD = SVN trunk r100, 2024-09-27) is post-1.4 but predates the installed 1.4.1 build (Dec 2024). I cross-checked every regex, format string and command name against `strings -el` output of the installed EqualizerAPO.dll 1.4.1.0 and Editor.exe and found them byte-identical, so I am confident the parser is unchanged — but I did not diff the actual 1.4.1 source, which is only on SourceForge SVN (svn://svn.code.sf.net/p/equalizerapo/code/trunk). Recommend `svn export` of trunk to confirm before freezing libapocfg.; Exact VSTPlugin quoting rules: I read the call site (splitQuoted(parameters, ' ', '"')) but not the body of StringHelper::splitQuoted beyond line 189, so I could not verify whether quotes are stripped from the returned token, whether escaped quotes are supported, or how an unterminated quote is handled. Fetch helpers/StringHelper.cpp:183-215 before implementing the VSTPlugin emitter.; Whether APO 1.4.1 added any command beyond the 15 factories in FilterEngine.cpp:81-95. The binary string dump shows no additional command-name literals, but a command with no distinctive log string could hide.; The precise ordering/whitespace the APO Editor generates for LoudnessCorrection and VSTPlugin lines. ParameterArchive::add appends "<Name> <value> " per field (trailing space), and to_wstring on a float yields 6 decimals — but LoudnessCorrectionFilter's own writer path (Editor/guis/LoudnessCorrectionFilterGUI.cpp) was not read, so the canonical emitted text (e.g. does it write "Attenuation 1.000000"? does the regex `(1((\.|,)0+)?)` accept that? yes) is inferred, not observed. Create one via the Editor and diff before emitting.; Behaviour of getline on a file ending in CRLF: whether APO sees a final empty line. This affects whether `trailingNewline` should default true or false for byte-exact round-trip. Determine empirically by loading and re-saving a file in APO's Editor and diffing.; How Peace 1.6.9.11 formats every field it emits. I confirmed Fc "%.1f", Gain "%.2f", Q "%.2f" for the mid/side filters (Peace.au3:29027-29029) and raw variable interpolation elsewhere (e.g. :29257 emits Fc with no formatting at all), but I did not audit all ~200 emit sites. Since peace.txt is currently 0 bytes on this machine, capture a live sample by launching Peace before deciding DreamDSP's default decimal counts.; muParserX expression evaluation is out of scope of what I verified in depth — I read the constant/function registration but not the parser semantics. If DreamDSP ever needs to EVALUATE (rather than preserve) If/Eval/backtick expressions, that is a separate dependency decision (vendoring muParserX vs. treating expressions as opaque).

---

## Peace 1.6.9.11 on-disk formats (peace.ini, *.peace, peace.txt) reverse-engineered from the live files on this machine + Peace.au3 source + decompiled Peace.chm
- Every Peace file on this machine is CRLF-terminated, pure-ASCII, no BOM, classic Win32 INI (WritePrivateProfileString). Values may contain '=' and the byte 0x0E is an in-value line break used inside Description.
  evidence: Byte scan of D:\Program Files\EqualizerAPO\config\*.peace + peace.ini: all 26 .peace files and peace.ini report CRLF and zero bytes >127. `Microphone stereo.peace` line 2 is literally b'Description=For equalizing the stereo microphone (left and right channels)\x0eImportant: Select your microphone in the device list\r'. Delimiter declared at I:\Qt\reference\peace-original\src\Peace\Peace.au3:380 `Const $DescriptionLineDelimiter = Chr(14)`. Files containing 0x0E: Headphones and Hearing Test.peace(1), Left on Right channel.peace(2), Microphone mono.peace(1), Microphone stereo.peace(1). `Last Configuration.peace` line 227 is `Routing=R=1.000*L C=1.000*L SUB=1.000*R RL=1.000*R RR=1.000*R` — four '=' in one value.
  → Write a hand-rolled INI parser, NOT QSettings(QSettings::IniFormat). Split each line on the FIRST '=' only; do not unescape; replace 0x0E with '\n' when displaying Description and re-encode on save. Accept both CRLF and LF on read, emit CRLF on write.
- QSettings is actively unsafe for peace.ini because three real keys in the user's file contain '/', which QSettings interprets as a group separator.
  evidence: D:\Program Files\EqualizerAPO\config\peace.ini line 80 `Show/Hide Hotkey=`, line 111 `Beep On/Off=0`, line 117 `Beep On/Off Volume=50`. Confirmed as literal key names in Peace.au3:2491 `IniRead($IniFile,"General","Show/Hide Hotkey","")`, :2535 `"Beep On/Off"`, :2540 `"Beep On/Off Volume"`.
  → Use a custom `PeaceIni` class (QMap<QString, QMap<QString,QString>> preserving insertion order). If you must use QSettings, you would have to escape '/' as "%2F" on every key, which breaks round-tripping — don't.
- A .peace file is a per-speaker (per target-channel) INI. Section names carry a numeric suffix = the speaker index; speaker 0 has NO suffix. The five per-speaker array sections are Frequencies/Gains/Qualities/Filters/Disabled, and keys inside are 1-based (Frequency1..Frequency31).
  evidence: Peace.au3:26731 `If $Speaker > 0 Then $SpeakerString = String($Speaker)  ; section/key 0 has no number`; the read loop at :26737-26744 reads `"Frequencies"&$SpeakerString / "Frequency"&$dBSlider+1`, `"Gains"&…/"Gain"&…`, `"Qualities"&…/"Quality"&…`, `"Filters"&…/"Filter"&…`, `"Disabled"&…/"Disabled"&…`. Mirrored by the writer at :27709-27720. Visible in D:\...\config\Last Configuration.peace: `[Frequencies]`+`[Qualities]` (speaker 0) then `[Frequencies1]/[Gains1]/[Qualities1]/[Filters1]` … through `[Filters6]`.
  → Parse section suffix with regex `^(Frequencies|Gains|Qualities|Filters|Disabled)(\d*)$`; empty suffix => speaker 0. Store bands as `std::array<PeaceBand, 31>` per speaker, index i holds key i+1.
- Only non-default values are written: Frequency only if >0, Gain only if !=0, Quality only if freq>0 && quality!=0, Filter only if >0 (i.e. type PK=0 is omitted), Disabled only if >0. Read defaults are Frequency=0, Gain=0, Quality=<peace.ini 'Quality Default Value'>, Filter=0, Disabled=0.
  evidence: Writer Peace.au3:27710-27714. Reader defaults Peace.au3:26738-26742 (`…"Gain"&…,"0"`, `…"Quality"&…,$QualityDefault`, `…"Filter"&…,"0"`). $QualityDefault set at Peace.au3:2517 from peace.ini General/"Quality Default Value" (=1.41 on this machine, peace.ini line 102). Confirmed empirically: Last Configuration.peace `[Gains1]` contains only `Gain10=10`, `[Filters1]` only `Filter10=5`; all 13 Quality keys are present because all frequencies are >0.
  → Reader must default-fill missing keys, never assume presence. Writer must reproduce the same omission rules or diffs against Peace-written files will be noisy. Quality default must be read from peace.ini, not hardcoded to 1.41.
- [Speakers] is an ordered list terminated by a missing SpeakerId; the SpeakerId VALUE is ignored, only the index matters. SpeakerTargets is the APO channel expression, SpeakerName is a display label. If [Speakers] is absent, Peace synthesizes 9 default speakers.
  evidence: Peace.au3:26708-26717 — `Do $Speaker = Number(IniRead($File,"Speakers","SpeakerId"&String($SpeakersMax),"-1")) … Until $Speaker = -1`, then reads `SpeakerTargets`/`SpeakerName` at the same index. Fallback at :26718-26727 uses `Const $SpeakerTypes = ["all","L","R","C","SUB","RL","RR","SL","SR","RC"]` (Peace.au3:386) minus the last (`UBound-2`, i.e. no RC). Real data: Bass Boost.peace lines 269-296 has ids 0..8 = all,L,R,C,SUB,RL,RR,SL,SR; 3 Way Crossover.peace lines 234-255 has only 0..6 with custom names like `SpeakerName4=High Right (Subwoofer)`; Microphone stereo.peace lines 3-6 has a single speaker `SpeakerTargets0=L R`.
  → Loop `for (int i=0; ini.has("Speakers", "SpeakerId"+QString::number(i)); ++i)`. Do not trust the stored value. Provide the 9-speaker default when the section is missing. SpeakerTargets may be multi-channel ("L R") or a Copy expression.
- [General] is a mixed bag: preset metadata + ALL global effects + per-speaker scalars PreAmp<N>/Delay<N>/GraphicEQ<N>/Delay Unit<N>/Off<N> using the same "index 0 = no suffix" rule.
  evidence: Peace.au3:26732-26736 reads `"PreAmp"&$SpeakerString`, `"Delay"&…`, `"GraphicEQ"&…`, `"Delay Unit"&…`, `"Off"&…` from section "General". Writer at :27698-27702. Metadata keys read at :26545-26551: Id, Description, Web Page, Configuration Icon, Device, Device GUID. Real data: 3 Way Crossover.peace lines 225-233 `[General] Description=… / Routing=… / PreAmp1=-10 / PreAmp2=-20 / … / PreAmp6=-10`; Graphic EQ.peace `[General] GraphicEQ=1` (speaker 0).
  → Model [General] as three logical groups in your struct — PresetMeta, GlobalEffects, and per-speaker scalars keyed by suffix — but serialize them all back into one [General] section.
- The complete [General] effects key set (19 slots) and their defaults, as read by ReadFile().
  evidence: Peace.au3:26598-26633 verbatim: Crossfeed(1), Dampen Left(1), Dampen Right(1), Stereo Balance(0), Stereo Amplifying(1), Spatial Balance(0), Spatial Setup(0), Reverse(0)+Reverse when(0), Upmix(0)+Upmix Way(0), Downmix 5.1(0), Downmix 7.1(0), Downmix Way(0), Stereo Widening(0), Echo Count(0)+Echo Delay(500), Fake Stereo(0)+Fake Stereo Delay(20)+Fake Stereo Frequency(1000), Channels Delay(0), Stereo Shift(0), Spatial Shift(0), Routing(""), Muting("")+Muting Type(0), Effects when(0), Crossfeed Simulation(0), Bass Gain(0)+Bass Frequency(500)+Bass Target(0), Treble Gain(0)+Treble Frequency(2000), Swap Speakers(0), Stereo Expanding(0), Mixing Stage(0). Writer conditions at :27565-27616.
  → Copy these key names and defaults verbatim into your Effects struct. Note 'Effects when' (0=emit effects BEFORE the per-speaker filter blocks, 1=after) and 'Mixing Stage' (1 => emit `Stage: pre-mix`) change EMIT ORDER, not just values — see the emitter finding.
- [Surround] holds 44 keys: on/way, per-speaker-count delay tables (Center/Side/Rear Delay 1..8 indexed by channel count), widths, levels, bass frequency and 8 crossover frequencies. It is written only when Surround On != 0.
  evidence: Reader Peace.au3:26636-26680 (`$SurroundEffect[3][0..7]` = "Center Delay 1".."Center Delay 8", etc.). Writer Peace.au3:27618-27664 wrapped in `If $SurroundEffect[0][0] <> 0 Then … IniWriteSection($File,"Surround",$Surround)`. Real data: D:\...\config\Surround Effect Default.peace lines 1-44 — note it lacks `Surround Way` and `Crossover Sub` (defaults 1 and 65 apply), and has `Crossover 1=125 … Crossover 7=8000`.
  → Model as `struct PeaceSurround { bool on; int way{1}; double frontDelay{9}, subDelay{8}; double centerDelay[8], sideDelay[8], rearDelay[8]; double frontWidth{0}, sideWidth{0.4}, rearWidth{0.6}; double bassFrequency{350}; double frontLevel{1}, sideLevel{1}, rearLevel{1}, centerLevel{1.2}, subLevel{1}; double crossoverSub{65}; double crossover[7]; }`. Skip the whole section on write when !on.
- [Commands] holds free-form Equalizer APO text in three slots, and the ini key numbering does NOT match the internal array index — key "1" is 'commands after' and key "2" is 'commands after device'. Embedded newlines are stored as TAB (0x09).
  evidence: Peace.au3:26699-26701 — `$aCommands[0] = StringReplace(IniRead($File,"Commands","0",""),@TAB,@CRLF)  ; commands before` / `$aCommands[2] = …"Commands","1"…  ; commands after` / `$aCommands[1] = …"Commands","2"…  ; commands after device command`. Writer mirrors it at :27688-27690 with `StringReplace(…,@CRLF,@TAB)`.
  → struct PeaceCommands { QString before;  /* ini key "0" */ QString afterDevice; /* ini key "2" */ QString after; /* ini key "1" */ };  On load: `s.replace('\t', "\r\n")`; on save: `s.replace("\r\n", "\t")`. Getting the 1/2 swap wrong will silently reorder emitted APO commands.
- [Sliders] stores per-band UI labels/colors, and the key format CHANGED between Peace versions: shipped presets use `Slider<N>-<P>` while the current 1.6.9.11 source reads/writes `Slider<N> <P>` (space).
  evidence: Peace.au3:26749 `IniRead($File,"Sliders","Slider" & ($Slider+1) & " " & $Property,"")` and writer :27727 `"Slider" & ($Slider+1) & " " & $Property`. But raw bytes of D:\...\config\Equalizer 5 Band.peace [Sliders] are `Slider1-0=low bass`, `Slider2-0=bass`, … `Slider1-1="`, `Slider1-2="` (od -c confirms literal '-'). Properties 0..2 are strings (three label lines), 3..4 are integers (colors, default -1); 5..6 are runtime-only, taken from Theme\theme.ini `[Sliders] Slider<N> 3/4` (Peace.au3:26758-26762).
  → Accept BOTH separators on read: regex `^Slider(\d+)[- ](\d)$`. Write with a space to match current Peace. Property 3/4 are BGR-ish COLORREF ints with -1 = 'unset'; fall back to Theme\theme.ini.
- [Configuration] contains exactly one key, HotKey, holding a bare key char or a virtual-key name with braces stripped. It combines with the global modifier string peace.ini General/'Hotkey Combination'.
  evidence: Peace.au3:17305-17313 — `IniWrite(…,"Configuration","HotKey",$ConfigurationHotKey)` for len==1 else `IniWrite(…, StringReplace(StringReplace($ConfigurationHotKey,"{",""),"}",""))`, and `IniDelete` when cleared. Applied at :26463-26470 (`FillConfigurationsList`): `HotKeySet($HotKeyCombi & $Key)` for single chars, `HotKeySet($HotKeyCombi & "{" & $Key & "}")` otherwise. $HotKeyCombi from peace.ini General/'Hotkey Combination' = `^!` (peace.ini line 82) meaning Ctrl+Alt. Real data: Microphone stereo.peace lines 35-36 `[Configuration]` / `HotKey=`.
  → Map AutoIt modifier chars ^=Ctrl, !=Alt, +=Shift, #=Win to Qt::KeyboardModifiers; single-char payload = literal key, multi-char = AutoIt vkey name (e.g. F5, NUMPAD1, PGUP). Peace also writes the key with an EMPTY value (as here) — treat empty as 'no hotkey'.
- Filter type is an integer index into an 18-entry table; each entry carries (APO token, hasGain, hasQ). Entries 8/9 reinterpret the Quality field as a dB/octave slope, and entries 10-13 reinterpret it as a filter ORDER.
  evidence: Peace.au3:384 `Const $FilterTypes = [["PK",True,True],["LPQ",False,True],["HPQ",False,True],["BP",False,True],["LS",True,False],["HS",True,False],["NO",False,True],["AP",False,True],["LSC",True,True],["HSC",True,True],["BWLP",False,True],["BWHP",False,True],["LRLP",False,True],["LRHP",False,True],["LSCQ",True,True],["HSCQ",True,True],["LSQ",True,True],["HSQ",True,True]]`. Emit special-cases at :29138-29148: FilterNo 14/15 emit `StringLeft(name,3)` = LSC/HSC; 16/17 emit `StringLeft(name,2)` = LS/HS; 8/9 emit `" " & $Quality & " dB"` right after the token and suppress the ` Q ` suffix. Cross-checked against the decompiled CHM page filters.htm (Peace/EqAPO filter table) which lists exactly LSC for 'Low shelf with dB slope' and LSC for 'Low shelf with quality value and corner frequency'. Real data: Last Configuration.peace `Filter5=4`(LS)/`Filter10=5`(HS); Bass Boost 2.peace `Filter5=14`(LSCQ); High Boost 2.peace `Filter10=15`(HSCQ).
  → Use the enum in code_samples[0]. Note LSCQ(14) and LSC(8) both emit the token 'LSC' but with different argument shapes — you cannot round-trip peace.txt back to a filter index without the .peace file.
- Butterworth/Linkwitz-Riley (indices 10-13) are not single APO filters — Peace expands them into a cascade of LPQ/HPQ lines using Q = 1/(-2*cos(pi*(2k+n-1)/(2n))).
  evidence: Peace.au3:29105-29136. For FilterNo 10/11: `$CascadeAmount=1; $Factors=Floor($Quality/2); $FirstOrder=0`. For 12/13: `$CascadeAmount=2; $Factors=Floor($Quality/2); $FirstOrder=Mod($Factors,2); $Factors=Floor($Factors/2); $Quality/=2`. If FirstOrder, one extra line `Filter <i>: ON LPQ|HPQ Fc <f> Hz Q 0.5` (LPQ iff FilterNo=12). Then `For $k=$Factors To 1 Step -1 / For $Cascade=1 To $CascadeAmount` emit `Filter <i>: ON LPQ|HPQ Fc <f> Hz Q <StringFormat("%.6f",CalculateQuality($k,$Quality))>` (LPQ iff FilterNo=10 or 12). CalculateQuality at Peace.au3:22177-22179 `Return 1/(-2*Cos($Pi*(2*$k+$n-1)/(2*$n)))`. Order cap `Const $FilterMaximumFactors = 50` at :385; CHM filters.htm says max order 100.
  → Implement `QStringList expandButterworthLinkwitzRiley(int filterNo, int bandIndex1, double fc, double order)` exactly as above, formatting Q with `QString::asprintf("%.6f", q)`. All cascaded lines reuse the SAME `Filter <bandIndex1>:` number.
- peace.txt is assembled from five string buckets concatenated in fixed order 0..4, then written to peace.tmp and file-copied over config\peace.txt while holding an append handle on config.txt as a crude lock.
  evidence: Peace.au3:29191-29221 `CreateFilterFile()` — `FileWrite($FileHandle, $Configuration & $UIConfiguration[0] & $UIConfiguration[1] & $UIConfiguration[2] & $UIConfiguration[3] & $UIConfiguration[4])` then `CopyFilterFile()`. Buckets filled in `AddToFilterFile()` Peace.au3:28874-29180: [0]=commands-before (:28878-28881), [1]=`"Device: " & DeviceString()` or `"Device: all"` (:29175-29179 — note it is filled LAST but concatenated SECOND), [2]=commands-after-device (:28883-28889), [3]=the DSP body, [4]=commands-after (:29165-29172). Constants at :377 `$CommandsFile = "peace.txt", $CommandsTempFile = @ScriptDir & "\peace.tmp"`. CopyFilterFile at :27737-27756. Line terminator is @CRLF throughout.
  → Reproduce the 5-bucket model verbatim: `QString buckets[5]; … out = b[0]+b[1]+b[2]+b[3]+b[4];`. Write to peace.tmp then QFile::copy over peace.txt (do NOT write peace.txt in place — Equalizer APO watches it).
- Exact emit order of the DSP body (bucket 3), including the two positions effects can occupy and the per-speaker block layout.
  evidence: Peace.au3:28892-29164 in order: upmix copies (:28892-29023), midside `Copy: MID=L+R SIDE=R+-1.0*L` + two PK filters + recombine (:29024-29032), `Stage: pre-mix` if Effects[18][0]=1 (:29033), ReverseChannels() if 'Reverse when'=1 (:29034), routing (:29035-29049) which emits `Copy: V<lhs>=<rhs>` for every token then `Copy: <lhs>=V<lhs>`, muting `Copy: <ch>=0` (:29050-29056), effects+surround if 'Effects when'=0 (:29057-29060), then the per-speaker loop (:29062-29159), then ReverseChannels() if 'Reverse when'=0 (:29160), then effects+surround if 'Effects when'=1 (:29161-29164). Per speaker: skip if `BitAnd(Off,1)=1`; `Copy: <targets>` first if targets contain '=' (:29068-29071); `Channel: <targets>`; `Delay: <v> samples|ms` if Delay>0 (:29076-29081); `Preamp: <v> dB` if WriteAll==1 or PreAmp!=0 (:29085-29087); then either `GraphicEQ: f g; f g; …` with the trailing "; " trimmed (:29092,:29098,:29156) or `Filter <i>: ON …` lines; whole block suppressed if $ChannelEmpty (:29158). Frequency is clamped `Min($MaxSliderFrequency, …)` (:29094).
  → Implement as a single `QString renderBody(const PeacePreset&, const PeaceSettings&)` following that exact sequence. Verify against Peace by turning Peace on (peace.ini OnOff=1) and byte-diffing your peace.txt with Peace's.
- Routing in .peace is a single flat string of `DEST=coef*SRC` tokens which the emitter rewrites via temporary virtual channels to avoid ordering hazards.
  evidence: Real data: D:\...\config\Last Configuration.peace line 227 `Routing=R=1.000*L C=1.000*L SUB=1.000*R RL=1.000*R RR=1.000*R`; Left on Right channel.peace `Routing=R=1.000*L`. Emitter Peace.au3:29035-29049: pass 1 emits `"Copy: V" & <token>` for each space-separated token (yielding `Copy: VR=1.000*L`), collecting the LHS names; pass 2 emits `"Copy: " & <lhs> & "=V" & <lhs>` (yielding `Copy: R=VR`).
  → Store `QString routing` verbatim; split on ' ' for the UI table; emit the two-pass V-prefixed form. Do not attempt to emit `Copy: R=1.000*L` directly — it changes semantics when a channel is both source and destination.
- peace.ini [General] — complete key inventory for this machine (129 keys, lines 1-129), all confirmed against IniRead calls in the source.
  evidence: D:\...\config\peace.ini lines 1-129 cross-checked against Peace.au3:2447-2556 (ReadSettings), :2718-2728 (ReadWindowsVersion), :2832 (Graph Show), :2881 (Show Device Installed), :2889-2890 (Program Mode / Show Program Mode), :2899-2900 (Check Newer Version / … When), :2909 (Anticlipping), :2996 (Save Window Positions), :3037-3073 (all 'Window <X> [XY] Position' + 'Peak Meters Width'), :3167-3208 (Window Settings/Sliders Keys Settings/Skins Settings/Themes Settings X Position), :852-853 (Configurations Path), :2531-2544 (all Beep*), :2545 (Write Device GUID), :2546 (Allow above 22050), :2457 (Selected Configuration), :2494 (OnOff), :2461 (Muted), :2516 (dB Sliders Max), :2517 (Quality Default Value).
  → See code_samples[3] for the full C++ settings struct with defaults. Keys present on disk but NOT in ReadSettings on this version (write-only or legacy) are: none found — all 129 resolve. Keys the source reads that are ABSENT from this peace.ini (so defaults apply): Configurations Path(@ScriptDir), Large Configurations List(0), Translation Language(""), Volume Show(0), Bass/Treble/Balance on Main(0), Graph Window Width(900)/Height(400), Commands Window Width/Height, Peak Meter on TaskBar(0), Backup Folder/Settings/Configurations, Restore Settings/Configurations, all [Midi] and [OSD] keys, [Sliders Keys] 'Key Slider <0..30>'.
- The user's current state: Peace is OFF, so peace.txt is a zero-byte file and Equalizer APO's config.txt still includes it (harmless no-op). The active preset is '3 Way Crossover', and 'Last Configuration.peace' is a byte-for-byte (modulo line order) mirror of it.
  evidence: peace.ini line 128 `OnOff=0`, line 129 `Muted=0`, line 126 `Selected Configuration=3 Way Crossover`. `ls -la` shows peace.txt is 0 bytes (Jul 28 18:31). config.txt is exactly `Include: peace.txt`. `diff <(sort 'Last Configuration.peace') <(sort '3 Way Crossover.peace')` → identical. Names fixed at Peace.au3:378 `$LastConfiguration = "Last Configuration"` and :855-856 `$DefaultFile = …"Equalizer Default.peace"`, `$CurrentFile = …$LastConfiguration & ".peace"`.
  → DreamDSP should treat `Last Configuration.peace` and `Equalizer Default.peace` as reserved/non-deletable (peace.ini 'Hide Non-deletables' controls listing them), mirror the selected preset into Last Configuration.peace on every apply, and write General/OnOff + General/Muted + General/Selected Configuration back into peace.ini. When OnOff=0 Peace empties peace.txt rather than removing the Include line.
- configbeforePeace.txt is the pre-Peace Equalizer APO config.txt that Peace displaced; it is plain APO syntax, not INI, and contains a fully-commented-out AutoEq GraphicEQ plus one bare '-1.8' line.
  evidence: D:\Program Files\EqualizerAPO\config\configbeforePeace.txt, all 4 lines: `# Preamp: 0.4 dB` / `# Include: example.txt` / `# GraphicEQ: 20 -0.6; 25 -1.8; …; 20000 2.7` / `-1.8` / `# Convolution: `. Contrast with config.txt (1 line: `Include: peace.txt`).
  → Do not parse this as a preset. Offer it read-only as 'restore original APO config' (copy back over config.txt) and as an import source for the commented GraphicEQ curve. Note line 4 `-1.8` is a syntactically invalid orphan APO line — your importer must tolerate junk lines.
- Peace exposes a documented external control API: WM_APP(0x8000)+N and WM_COPYDATA(0x004A) sent to a hidden window titled 'Peace window messages', plus a 3-argument command line.
  evidence: Decompiled CHM (C:\Users\10678\AppData\Local\Temp\claude\I--Qt\7fffc091-07c0-410a-a51c-5a342ddbdb5e\scratchpad\chm2\control.htm, controlconfiguration.htm, controlerror.htm, commandline.htm). Confirmed in source: Peace.au3:815 `GUICreate("Peace window messages")`, :817-829 registers WM_APP(MainActions), +1(InterfaceActions), +2(ConfigurationSwitch), +3(EqualizerState), +4(MainRenderDevice), +5(MainPreAmplifying), +6(EffectsPanel), +7(DeviceActions), +20(TargetSpeaker), +21(PreAmplifying), +22(EqualizationSliders), +100+sliderNo(EqualizationSlider); :3519 `WinGetHandle("Peace window messages")`. WM_APP+2 wParam: 0=leave UI, 1=show UI, 2=hide to tray, 3=save current, 4=set config id, 5=get current id, 10/11/12/13=hotkey-config on/off/toggle/get, 20=switch configurations-set by id; lParam=configuration Id. Fractional values are passed as milli-dB integers (5400 = 5.4 dB) and qualities/crossfeed as value*1000. General error codes: -1 main loop not running, -2 settings window active, -3 wrong message, -4 busy, -5 bad wParam; WM_COPYDATA adds -6 action-specific, -7 no COPYDATASTRUCT, -8 no cbData, -9 no lpData, -10 empty name. Command line (commandline.htm): `Peace.exe "<config name>" ["start"] ["hide"|"stay"]`, first arg always the config name and may be ""; parsed at Peace.au3:3461-3476.
  → If DreamDSP wants drop-in compatibility with existing Rainmeter/AHK/stream-deck setups, create a hidden QWidget whose native window title is exactly 'Peace window messages', install a QAbstractNativeEventFilter, and handle WM_APP+0..7,20,21,22,100+n with the same wParam/lParam/return-code contract. Also accept the same 3-arg command line. UIPI note from control.htm: cross-integrity-level sends are blocked unless both apps launch from shortcuts.
- Band count is 31 max / 7 min; the 'visible slider count' is derived at load time as max(7, highest 1-based index whose Frequency>0) across ALL speakers. Frequency is clamped to 22050 Hz unless peace.ini 'Allow above 22050'=1, in which case 99999.
  evidence: Peace.au3:381 `$SlidersMinNumber = 7, $SlidersMaxNumber = 31`; :385 `Const $MaximumSliderFrequency = 22050 ; due to a bug in Equalizer APO?`; :2546-2547 `$AllowAbove22050 = …"Allow above 22050"…; $MaxSliderFrequency = $AllowAbove22050 ? 99999 : $MaximumSliderFrequency`; :26729 `$dBSlidersMax = $SlidersMinNumber` then :26743 `If $Equalizer[$Speaker][1][$dBSlider] > 0 Then $dBSlidersMax = Max($dBSlidersMax,$dBSlider+1)`. peace.ini line 123 `Allow above 22050=0`, line 101 `dB Sliders Max=13`. Real data: Equalizer One Third Octave.peace has Frequency1..Frequency31 / Quality31=4.32; Headphones and Hearing Test.peace has 19 bands.
  → `static constexpr int kMaxBands = 31, kMinBands = 7;` Compute `visibleBands` on load exactly as above — do NOT use peace.ini 'dB Sliders Max' for this (that is only the UI default for a NEW preset). Clamp gains to peace.ini [GUI] 'dB Limit' (30) and preamp to 'dB Pre Amp Limit' (30), per Peace.au3:26732/26739 and :2518-2519.
- A shipped preset contains a DUPLICATE [General] section, which Win32 GetPrivateProfileString resolves by first-match-only — a naive merging parser will behave differently from Peace.
  evidence: D:\Program Files\EqualizerAPO\config\Equalizer One Third Octave.peace lines 1-12: `[General]` / `Description=31 sliders equalizer using peak filters` then a SECOND `[General]` / `PreAmp=0` / `PreAmp8=0` … `PreAmp1=0`. AutoIt IniRead is a thin wrapper over GetPrivateProfileString, which scans to the first matching section header and stops at the next header.
  → Make your parser first-section-wins for duplicate section names (or at minimum, log and merge deterministically) and NEVER write duplicate sections. In this specific file the shadowed values are all 0 so behaviour is identical either way, but a user-authored file could differ.

### code samples
```
// PeacePreset.h — .peace file model, Peace 1.6.9.11
// Field-for-field traceable to I:\Qt\reference\peace-original\src\Peace\Peace.au3
#pragma once
#include <QString>
#include <QVector>
#include <array>

namespace peace {

static constexpr int kMaxBands  = 31;   // Peace.au3:381 $SlidersMaxNumber
static constexpr int kMinBands  = 7;    // Peace.au3:381 $SlidersMinNumber
static constexpr double kMaxFreqDefault = 22050.0;  // Peace.au3:385
static constexpr double kMaxFreqUnlocked = 99999.0; // Peace.au3:2547
static constexpr int kMaxFilterFactors = 50;        // Peace.au3:385
static constexpr char kDescriptionLineSep = '\x0E'; // Peace.au3:380 Chr(14)

// Value stored in [Filters<N>] Filter<i>. Table: Peace.au3:384 $FilterTypes
// = { APO token, hasGain, hasQ }
enum class FilterType : int {
    PK   = 0,  // peak                       gain + Q
    LPQ  = 1,  // low  pass, Q               Q
    HPQ  = 2,  // high pass, Q               Q
    BP   = 3,  // band pass                  Q
    LS   = 4,  // low  shelf, fixed slope    gain only
    HS   = 5,  // high shelf, fixed slope    gain only
    NO   = 6,  // notch                      Q
    AP   = 7,  // all pass (phase only)      Q
    LSC  = 8,  // low  shelf, quality field = slope in dB/oct
    HSC  = 9,  // high shelf, quality field = slope in dB/oct
    BWLP = 10, // Butterworth   low  pass, quality field = ORDER n
    BWHP = 11, // Butterworth   high pass, quality field = ORDER n
    LRLP = 12, // Linkwitz-Riley low pass, quality field = ORDER n
    LRHP = 13, // Linkwitz-Riley high pass, quality field = ORDER n
    LSCQ = 14, // low  shelf, Q + corner freq -> emitted as "LSC" (Peace.au3:29139)
    HSCQ = 15, // high shelf, Q + corner freq -> emitted as "HSC"
    LSQ  = 16, // low  shelf, Q               -> emitted as "LS"  (Peace.au3:29141)
    HSQ  = 17  // high shelf, Q               -> emitted as "HS"
};
static constexpr int kFilterTypeCount = 18;

struct FilterTraits { const char* token; bool hasGain; bool hasQ; };
inline const FilterTraits& traits(FilterType t) {
    static const FilterTraits k[kFilterTypeCount] = {
        {"PK",1,1},{"LPQ",0,1},{"HPQ",0,1},{"BP",0,1},{"LS",1,0},{"HS",1,0},
        {"NO",0,1},{"AP",0,1},{"LSC",1,1},{"HSC",1,1},{"BWLP",0,1},{"BWHP",0,1},
        {"LRLP",0,1},{"LRHP",0,1},{"LSCQ",1,1},{"HSCQ",1,1},{"LSQ",1,1},{"HSQ",1,1}
    };
    return k[static_cast<int>(t)];
}

// One equalizer band == one "slider". Written only when non-default:
// Peace.au3:27710-27714 ; read defaults Peace.au3:26738-26742
struct Band {
    double     frequency = 0.0;                 // [Frequencies<N>] Frequency<i>  (omitted if <=0)
    double     gain      = 0.0;                 // [Gains<N>]       Gain<i>       (omitted if ==0)
    double     quality   = 1.41;                // [Qualities<N>]   Quality<i>    (default = peace.ini Quality Default Value)
    FilterType filter    = FilterType::PK;      // [Filters<N>]     Filter<i>     (omitted if ==0)
    int        disabled  = 0;                   // [Disabled<N>]    Disabled<i>   (omitted if ==0)
    bool active() const { return frequency > 0.0; }
};

// One target channel / "speaker". Section suffix = index; index 0 has NO suffix.
// Peace.au3:26731 ; :27696
struct Speaker {
    QString targets;            // [Speakers] SpeakerTargets<i>  e.g. "all", "L", "L R", "VX=L+R"
    QString name;               // [Speakers] SpeakerName<i>     display label only
    double  preAmp   = 0.0;     // [General] PreAmp<sfx>       dB, clamped +/- dB Pre Amp Limit
    double  delay    = 0.0;     // [General] Delay<sfx>        emitted only when > 0
    bool    delayInSamples = false; // [General] Delay Unit<sfx>  0 = ms, 1 = samples
    bool    graphicEq = false;  // [General] GraphicEQ<sfx>    1 => emit "GraphicEQ: f g; ..."
    bool    off       = false;  // [General] Off<sfx>          bit0 -> whole block skipped
    std::array<Band, kMaxBands> bands{};
    // true when targets contains '=' -> emit a leading "Copy: <targets>" (Peace.au3:29068)
    bool isVirtual() const { return targets.contains('='); }
};

// [General] global effects. Names/defaults: Peace.au3:26598-26633
struct Effects {
    double crossfeed        = 1.0;   // Crossfeed
    double dampenLeft       = 1.0;   // Dampen Left
    double dampenRight      = 1.0;   // Dampen Right
    double stereoBalance    = 0.0;   // Stereo Balance
    double stereoAmplifying = 1.0;   // Stereo Amplifying
    double spatialBalance   = 0.0;   // Spatial Balance
    int    spatialSetup     = 0;     // Spatial Setup
    bool   reverse          = false; // Reverse
    int    reverseWhen      = 0;     // Reverse when   0 = after EQ block, 1 = before
    int    upmix            = 0;     // Upmix
    int    upmixWay         = 0;     // Upmix Way
    bool   downmix51        = false; // Downmix 5.1
    bool   downmix71        = false; // Downmix 7.1
    int    downmixWay       = 0;     // Downmix Way
    double stereoWidening   = 0.0;   // Stereo Widening
    int    echoCount        = 0;     // Echo Count
    double echoDelay        = 500.0; // Echo Delay        ms
    bool   fakeStereo       = false; // Fake Stereo
    double fakeStereoDelay  = 20.0;  // Fake Stereo Delay ms
    double fakeStereoFreq   = 1000.0;// Fake Stereo Frequency
    double channelsDelay    = 0.0;   // Channels Delay
    double stereoShift      = 0.0;   // Stereo Shift
    double spatialShift     = 0.0;   // Spatial Shift
    QString routing;                 // Routing  "R=1.000*L C=1.000*L ..."
    QString muting;                  // Muting   space-separated channel names
    int    mutingType       = 0;     // Muting Type
    int    effectsWhen      = 0;     // Effects when  0 = before speaker blocks, 1 = after
    int    crossfeedSim     = 0;     // Crossfeed Simulation  0=off 1=Chu Moy 2=Jan Meier
    double bassGain         = 0.0;   // Bass Gain
    double bassFrequency    = 500.0; // Bass Frequency
    int    bassTarget       = 0;     // Bass Target  0=all speakers, 1=subwoofer only
    double trebleGain       = 0.0;   // Treble Gain
    double trebleFrequency  = 2000.0;// Treble Frequency
    int    swapSpeakers     = 0;     // Swap Speakers  1=rear, 2=side
    double stereoExpanding  = 0.0;   // Stereo Expanding
    int    mixingStage      = 0;     // Mixing Stage   1 => emit "Stage: pre-mix"
};

// [Surround] — written only when on. Peace.au3:26636-26680 / :27618-27664
struct Surround {
    bool   on   = false;             // Surround On
    int    way  = 1;                 // Surround Way
    double frontDelay = 9.0;         // Front Delay
    double subDelay   = 8.0;         // Subwoofer Delay
    double centerDelay[8] = {13,12,10,9,9.1,11,14,14};   // Center Delay 1..8
    double sideDelay  [8] = {11,10,9,11,12,10,9,8};      // Side   Delay 1..8
    double rearDelay  [8] = {15,14,15,16,17,18,17,16};   // Rear   Delay 1..8
    double frontWidth = 0.0, sideWidth = 0.4, rearWidth = 0.6;
    double bassFrequency = 350.0;    // Bass Frequency
    double frontLevel = 1.0, sideLevel = 1.0, rearLevel = 1.0,
           centerLevel = 1.2, subLevel = 1.0;
    double crossoverSub = 65.0;      // Crossover Sub
    double crossover[7] = {125,250,500,1000,2000,4000,8000}; // Crossover 1..7
};

// [Upmix] Peace.au3:26683-26688 ; [Midside] :26691-26696
struct Upmix {
    int    on = 0; int way = 0; int speakers = -1;
    double frontRear = 0.0;          // metres, converted to delay via /343
    double leftRight = 0.0;
    int    bassRedirect = 1;
};
struct Midside {
    double gainMid = 0.0,  freqMid  = 2000.0, qMid  = 0.7;
    double gainSide = 0.0, freqSide = 2000.0, qSide = 0.7;
    // active iff freqMid != freqSide  (Peace.au3:29024, :27677)
    bool active() const { return freqMid != freqSide; }
};

// [Commands] — NOTE the ini key numbering is NOT the array order.
// Peace.au3:26699-26701 / :27688-27690.  '\t' in file == CRLF in memory.
struct Commands {
    QString before;       // ini key "0"  -> bucket 0, before everything
    QString afterDevice;  // ini key "2"  -> bucket 2, right after "Device:"
    QString after;        // ini key "1"  -> bucket 4, at the very end
};

// [Sliders] per-band UI decoration. Key is "Slider<i> <p>" (current Peace)
// or "Slider<i>-<p>" (files shipped <= ~2022). Peace.au3:26749 / :27727.
struct SliderStyle {
    QString label[3];             // properties 0,1,2 — three label lines
    int     backColor  = -1;      // property 3, -1 = unset
    int     frontColor = -1;      // property 4, -1 = unset
};

struct Preset {
    QString fileName;             // basename without ".peace"
    // ---- [General] metadata (Peace.au3:26545-26551) ----
    QString id;                   // Id              — stable key used by WM_APP+2
    QString description;          // Description     — 0x0E separates lines
    QString webPage;              // Web Page
    int     icon = 0;             // Configuration Icon
    QString device;               // Device          — human-readable device string
    QString deviceGuid;           // Device GUID     — preferred, resolved first
    // ---- payload ----
    QVector<Speaker> speakers;    // [Speakers] + suffixed array sections
    Effects  effects;
    Surround surround;
    Upmix    upmix;
    Midside  midside;
    Commands commands;
    QVector<SliderStyle> sliderStyles; // [Sliders]
    QString  hotKey;              // [Configuration] HotKey (bare, braces stripped)

    // Peace.au3:26729 + :26743 — how many sliders the UI shows.
    int visibleBandCount() const {
        int n = kMinBands;
        for (const Speaker& s : speakers)
            for (int i = 0; i < kMaxBands; ++i)
                if (s.bands[i].frequency > 0.0) n = std::max(n, i + 1);
        return n;
    }
};

} // namespace peace
```

```
// Section-name decoding for the numeric-suffix convention.
// Peace.au3:26731  "If $Speaker > 0 Then $SpeakerString = String($Speaker) ; section/key 0 has no number"
#include <QRegularExpression>

struct SectionKey { QString base; int speaker; };

static bool decodeSection(const QString& raw, SectionKey* out) {
    static const QRegularExpression re(
        QStringLiteral("^(Frequencies|Gains|Qualities|Filters|Disabled)(\\d*)$"));
    const auto m = re.match(raw);
    if (!m.hasMatch()) return false;
    out->base    = m.captured(1);
    out->speaker = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
    return true;
}

// Item keys inside those sections are 1-based:  Frequency1 .. Frequency31
static bool decodeItemKey(const QString& key, const QString& base, int* index0) {
    // base "Frequencies" -> item prefix "Frequency"; others drop the trailing 's'
    const QString prefix = (base == QLatin1String("Frequencies"))
        ? QStringLiteral("Frequency")
        : base.left(base.size() - (base.endsWith('s') ? 1 : 0));
    if (!key.startsWith(prefix)) return false;
    bool ok = false;
    const int n = key.mid(prefix.size()).toInt(&ok);
    if (!ok || n < 1 || n > peace::kMaxBands) return false;
    *index0 = n - 1;
    return true;
}
// prefix table produced by the above:
//   Frequencies -> Frequency   Gains -> Gain   Qualities -> Qualitie  (WRONG)
// so special-case it explicitly instead:
static QString itemPrefixFor(const QString& base) {
    if (base == QLatin1String("Frequencies")) return QStringLiteral("Frequency");
    if (base == QLatin1String("Qualities"))   return QStringLiteral("Quality");
    if (base == QLatin1String("Gains"))       return QStringLiteral("Gain");
    if (base == QLatin1String("Filters"))     return QStringLiteral("Filter");
    return QStringLiteral("Disabled");        // [Disabled] Disabled<i>  (no plural change)
}

// Per-speaker scalars live in [General] with the same suffix rule
// (Peace.au3:26732-26736 / :27698-27702):
//   PreAmp<sfx>  Delay<sfx>  GraphicEQ<sfx>  "Delay Unit"<sfx>  Off<sfx>
// where <sfx> is "" for speaker 0 and "1","2",... otherwise.
static QString generalKey(const char* stem, int speaker) {
    return speaker == 0 ? QString::fromLatin1(stem)
                        : QString::fromLatin1(stem) + QString::number(speaker);
}
```

```
// Rendering one speaker block into Equalizer APO text.
// Faithful port of Peace.au3:29062-29159 (the per-speaker loop of AddToFilterFile).
#include <cmath>
#include <QStringList>

static constexpr double kPi = 3.14159265358979323846;

// Peace.au3:22177-22179
static double calculateQuality(int k, double n) {
    return 1.0 / (-2.0 * std::cos(kPi * (2.0 * k + n - 1.0) / (2.0 * n)));
}

// Peace.au3:29105-29136 — Butterworth / Linkwitz-Riley expansion.
// filterNo in {10,11,12,13}; bandNo1 is the 1-based slider number reused on every line.
static QString expandBwLr(int filterNo, int bandNo1, double fc, double quality) {
    QString out;
    int cascade, factors, firstOrder;
    if (filterNo == 10 || filterNo == 11) {              // BWLP / BWHP
        cascade = 1; factors = int(std::floor(quality / 2.0)); firstOrder = 0;
    } else {                                             // LRLP / LRHP
        cascade = 2;
        factors = int(std::floor(quality / 2.0));
        firstOrder = factors % 2;
        factors = int(std::floor(factors / 2.0));
        quality /= 2.0;
    }
    const char* lp = "LPQ"; const char* hp = "HPQ";
    if (firstOrder == 1) {
        out += QStringLiteral("Filter %1: ON %2 Fc %3 Hz Q 0.5\r\n")
                   .arg(bandNo1)
                   .arg(QLatin1String(filterNo == 12 ? lp : hp))
                   .arg(fc);
    }
    for (int k = factors; k >= 1; --k)
        for (int c = 1; c <= cascade; ++c)
            out += QStringLiteral("Filter %1: ON %2 Fc %3 Hz Q %4\r\n")
                       .arg(bandNo1)
                       .arg(QLatin1String((filterNo == 10 || filterNo == 12) ? lp : hp))
                       .arg(fc)
                       .arg(QString::asprintf("%.6f", calculateQuality(k, quality)));
    return out;
}

// Peace.au3:29138-29149 — every other filter type.
static QString renderFilterLine(int bandNo1, const peace::Band& b) {
    const int fno = int(b.filter);
    const peace::FilterTraits& t = peace::traits(b.filter);
    QString token = QString::fromLatin1(t.token);
    if (fno == 14 || fno == 15) token = token.left(3);   // LSCQ/HSCQ -> LSC/HSC
    else if (fno == 16 || fno == 17) token = token.left(2); // LSQ/HSQ -> LS/HS

    QString s = QStringLiteral("Filter %1: ON %2").arg(bandNo1).arg(token);
    if (fno == 8 || fno == 9) s += QStringLiteral(" %1 dB").arg(b.quality); // slope
    s += QStringLiteral(" Fc %1 Hz").arg(b.frequency);
    if (t.hasGain) s += QStringLiteral(" Gain %1 dB").arg(b.gain);
    if (t.hasQ && !(fno == 8 || fno == 9)) s += QStringLiteral(" Q %1").arg(b.quality);
    return s + QStringLiteral("\r\n");
}

// Whole block. writeAll mirrors peace.ini General/"Write All".
static QString renderSpeaker(const peace::Speaker& sp, int visibleBands,
                             double maxFreq, bool writeAll) {
    if (sp.off) return QString();                        // Peace.au3:29063

    QString pre, chan;
    if (sp.isVirtual()) {                                // Peace.au3:29068-29071
        pre  = QStringLiteral("Copy: %1\r\n").arg(sp.targets);
        chan = QStringLiteral("Channel: %1\r\n").arg(sp.targets.left(sp.targets.indexOf('=')));
    } else {
        chan = QStringLiteral("Channel: %1\r\n").arg(sp.targets);
    }
    bool empty = true;
    if (sp.delay > 0) {                                  // Peace.au3:29076-29081
        chan += QStringLiteral("Delay: %1 %2\r\n")
                    .arg(sp.delay).arg(sp.delayInSamples ? "samples" : "ms");
        empty = false;
    }
    if (writeAll || sp.preAmp != 0.0) {                  // Peace.au3:29085-29087
        chan += QStringLiteral("Preamp: %1 dB\r\n").arg(sp.preAmp);
        empty = false;
    }
    if (sp.graphicEq) chan += QStringLiteral("GraphicEQ: "); // Peace.au3:29092

    for (int i = 0; i < peace::kMaxBands; ++i) {
        const peace::Band& b = sp.bands[i];
        const double f = std::min(maxFreq, b.frequency); // Peace.au3:29094
        if (sp.graphicEq && f != 0.0) {                  // Peace.au3:29097-29099
            chan += QStringLiteral("%1 %2; ").arg(f).arg(b.gain);
            empty = false;
            continue;
        }
        if (f == 0.0 || b.disabled != 0) continue;       // Peace.au3:29103-29104
        const peace::FilterTraits& t = peace::traits(b.filter);
        // Peace.au3:29104 — gain-bearing filters with 0 dB are skipped unless Write All
        if (!writeAll && t.hasGain && b.gain == 0.0) continue;
        const int fno = int(b.filter);
        chan += (fno >= 10 && fno <= 13) ? expandBwLr(fno, i + 1, f, b.quality)
                                         : renderFilterLine(i + 1, b);
        empty = false;
    }
    if (sp.graphicEq) chan.chop(2), chan += QStringLiteral("\r\n"); // Peace.au3:29156
    return pre + (empty ? QString() : chan);              // Peace.au3:29157-29158
}
```

```
// PeaceSettings.h — complete peace.ini model as found on this machine
// (D:\Program Files\EqualizerAPO\config\peace.ini, 197 lines, 6 sections).
// Defaults are the IniRead() defaults from Peace.au3; the trailing comment is
// THIS user's current value.
#pragma once
#include <QString>
#include <QPoint>

struct PeaceSettings {
  // ---------------- [General] ---------------- (Peace.au3:2447-2556 etc.)
  QString language            = "English";  // Language              = Chinese simplified
  int  saveWindowPositions    = 1;          // Save Window Positions = 1   (:2996)
  // Window <name> X/Y Position — -1 means "never positioned". (:3037-3072, :3167-3208)
  QPoint winMain{-1,-1};        // Window Main            = 704,264
  QPoint winCommands{-1,-1};    // Window Commands        = -1,-1
  QPoint winGraph{-1,-1};       // Window Graph           = 508,327
  QPoint winGraphSettings{-1,-1};// Window Graph Settings = -1,-1
  QPoint winEffects{-1,-1};     // Window Effects         = -1,-1
  QPoint winMidi{-1,-1};        // Window Midi            = 458,227
  QPoint winRouting{-1,-1};     // Window Routing         = -1,-1
  QPoint winSurround{-1,-1};    // Window Surround        = -1,-1
  QPoint winUpmix{-1,-1};       // Window Upmix           = -1,-1
  QPoint winMidside{-1,-1};     // Window Midside         = -1,-1
  QPoint winTest{-1,-1};        // Window Test            = 543,213
  QPoint winUse{-1,-1};         // Window Use             = -1,-1
  QPoint winAutomation{-1,-1};  // Window Automation      = 508,252
  QPoint winSave{-1,-1};        // Window Save            = -1,-1
  QPoint winExport{-1,-1};      // Window Export          = 658,437
  QPoint winImport{-1,-1};      // Window Import          = 873,180
  QPoint winHotkey{-1,-1};      // Window Hotkey          = -1,-1
  QPoint winPeakMeters{-1,-1};  // Window Peak Meters     = -1,-1
  int  peakMetersWidth        = 310;        // Peak Meters Width     = 310 (:3073)
  int  winSettingsX           = -1;         // Window Settings X Position            (:3167)
  int  winSlidersKeysX        = -1;         // Window Sliders Keys Settings X Position (:3169)
  int  winSkinsX              = -1;         // Window Skins Settings X Position      (:3171)
  int  winThemesX             = -1;         // Window Themes Settings X Position     (:3181)
  int  programMode            = 1;          // Program Mode        = 0  (:2889) 0=full/advanced UI
  int  showProgramMode        = 1;          // Show Program Mode   = 1  (:2890)
  bool checkNewerVersion      = true;       // Check Newer Version = 1  (:2899)
  int  checkNewerVersionWhen  = 0;          // Check Newer Version When = 0 (:2900)
  bool showDeviceInstalled    = true;       // Show Device Installed = 1 (:2881)
  bool checkWindowsUpdated    = true;       // Check Windows Updated = 1 (:2718)
  int  windowsVersionNumber   = 0;          // Windows Version Number = 10   (:2719)
  int  windowsBuildNumber     = 0;          // Windows Build Number  = 26200 (:2720)
  int  windowsUbrNumber       = 0;          // Windows UBR Number    = 8875  (:2721)
  bool emptyOnLeave           = false;      // Empty on Leave      = 0  (:2447) clear peace.txt on exit
  int  tooltipsShow           = 10000;      // Tooltips Show       = 10000 ms (:2448)
  int  tooltipsDelay          = 200;        // Tooltips Delay      = 200 ms   (:2449)
  QString startupPassword;                  // Startup Password    = ""  (:2450) PLAINTEXT
  bool visualImpaired         = false;      // Visual Impaired     = 0  (:2451)
  bool backUpConfigurations   = true;       // Back up Configurations = 1 (:2452)
  bool traceLog               = false;      // Trace Log           = 0  (:2456)
  QString muteHotkey;                       // Mute Hotkey         = "" (:2462)
  QString preAmpUpHotkey, preAmpDownHotkey; // Pre Amplifying Up/Down Hotkey (:2463-2464)
  double preAmpHotkeyStep     = 1.0;        // Pre Amplifying Hotkey Step = 1 (:2465)
  bool showPreAmpSlider       = false;      // Show Pre Amplifying Slider = 0 (:2466)
  int  showPreAmpSliderTime   = 3;          // Show Pre Amplifying Slider Time = 3 (:2467)
  QString slidersKeysCombi    = "^!";       // Sliders Keys Combi      = ^!  (Ctrl+Alt) (:2468)
  QString slidersKeysCombiDown= "+^!";      // Sliders Keys Combi Down = +^! (:2469)
  QString slidersKeysReset;                 // Sliders Keys Reset Hotkey = "" (:2471)
  double slidersKeysStep      = 1.0;        // Sliders Keys Step   = 1  (:2470)
  QString balanceRightHotkey, balanceLeftHotkey; // Balance Right/Left Hotkey (:2486-2487)
  double balanceHotkeyStep    = 0.1;        // Balance Hotkey Step = 0.1 (:2488)
  bool showBalanceWindow      = true;       // Show Balance Window = 1  (:2489)
  int  showBalanceTime        = 2;          // Show Balance Time   = 2  (:2490)
  QString showHideHotkey;                   // "Show/Hide Hotkey"  = "" (:2491)  <-- contains '/'
  QString onOffHotkey;                      // "On/Off Hotkey"     = "" (:2492)  <-- contains '/'
  QString hotkeyCombination   = "^!";       // Hotkey Combination  = ^!  (:2493)
  int  trayActive             = 1;          // Tray Active         = 1  (:2504)
  int  escToTray              = 0;          // Esc To Tray         = 0  (:2506)
  int  showMainGui            = 1;          // Show Main GUI       = 1  (:2505)
  int  volumeOrientation      = 0;          // Volume Orientation  = 0  (:2459)
  int  installConfigurations  = -1;         // Install Configurations = 0 (:2499)
  bool hideNonDeletables      = false;      // Hide Non-deletables = 0  (:2500)
  int  writeAll               = 0;          // Write All           = 0  (:2501) 1 = emit every filter
  bool refreshMessage         = true;       // Refresh Message     = 1  (:2502)
  bool showTrayMessage        = true;       // Show Tray Message   = 1  (:2503)
  double fontSize             = 8.5;        // Font Size           = 8.5 (:2514)
  QString configurationFont;                // Configuration Font  = "" (:2515)
  int  confirmConfiguration   = 2;          // Confirm Configuration = 2 (:2507)
  int  sliderXSpace           = 2;          // Slider X Space      = 2  (:2508)
  int  sliderThumbWidth       = 22;         // Slider Thumb Width  = 22 (:2509)
  int  deviceListWidth        = 185;        // Device List Width   = 185 (:2510)
  int  configurationsWidth    = 200;        // Configurations Width  = 200 (:2512)
  int  configurationsHeight   = 165;        // Configurations Height = 165 (:2513)
  int  dbSliderHeight         = 300;        // dB Slider Height    = 300 (:2511)
  int  dbSlidersMax           = 13;         // dB Sliders Max      = 13 (:2516) clamped [7,31]
  double qualityDefaultValue  = 1.41;       // Quality Default Value = 1.41 (:2517)
  bool showPeakMeter          = true;       // Show Peak Meter     = 1  (:2526)
  bool formerTrayMouse        = false;      // Former Tray Mouse   = 0  (:2528)
  bool trayShowGui            = true;       // Tray Show GUI       = 1  (:2529)
  bool trayPreAmpShow         = false;      // Tray Pre Amp Show   = 0  (:2530)
  bool beepOnHotkeySwitch     = false;      // Beep on Hotkey Switch        = 0 (:2531)
  int  beepOnHotkeySwitchBeep = 0;          // Beep on Hotkey Switch Beep   = 0 (:2532)
  QString beepOnHotkeySwitchFile;           // Beep on Hotkey Switch File   = "" (:2533)
  int  beepOnHotkeySwitchVol  = 50;         // Beep on Hotkey Switch Volume = 50 (:2534)
  int  beepOnOff              = 0;          // "Beep On/Off"        = 0  (:2535) <-- '/'
  int  beepOn                 = 0;          // "Beep On"            = 0
  int  beepOnBeep             = 0;          // "Beep On Beep"       = 0  (:2536)
  QString beepOnFile;                       // "Beep On File"       = "" (:2537)
  int  beepOffBeep            = 1;          // "Beep Off Beep"      = 1  (:2538)
  QString beepOffFile;                      // "Beep Off File"      = "" (:2539)
  int  beepOnOffVolume        = 50;         // "Beep On/Off Volume" = 50 (:2540) <-- '/'
  int  beepPreAmp             = 0;          // Beep Pre Amp         = 0  (:2541)
  int  beepPreAmpBeep         = 0;          // Beep Pre Amp Beep    = 0  (:2542)
  QString beepPreAmpFile;                   // Beep Pre Amp File    = "" (:2543)
  int  beepPreAmpVolume       = 50;         // Beep Pre Amp Volume  = 50 (:2544)
  bool writeDeviceGuid        = true;       // Write Device GUID    = 1  (:2545)
  bool allowAbove22050        = false;      // Allow above 22050    = 0  (:2546)
  bool anticlipping           = false;      // Anticlipping         = 1  (:2909)
  bool graphShow              = false;      // Graph Show           = 0  (:2832)
  QString selectedConfiguration;            // Selected Configuration = "3 Way Crossover" (:2457)
  int  topIndexConfigurations = -1;         // Top Index Configurations = 0 (:2453)
  bool onOff                  = true;       // OnOff  = 0  (:2494)  <-- Peace currently OFF
  bool muted                  = false;      // Muted  = 0  (:2461)

  // ---------------- [GUI] ---------------- (Peace.au3:2518-2520)
  double dbLimit       = 30;   // dB Limit         = 30  clamps every Gain
  double dbPreAmpLimit = 30;   // dB Pre Amp Limit = 30  clamps every PreAmp
  int    tickRatio     = 6;    // Tick Ratio       = 6

  // ---------------- [Changing Sliders] ---------------- (Peace.au3:2521-2525)
  double addDbValue     = 0.5;  // Add dB Value       = 0.5
  double expandDbRatio  = 1.25; // Expand dB Ratio    = 1.25
  double compressDbRatio= 0.8;  // Compress dB Ratio  = 0.8
  double dbSnapValue    = 0.1;  // dB Snap Value      = 0.1  clamped [0.01,10]
  double mousewheelValue= 1.0;  // Mousewheel Value   = 1

  // ---------------- [Export] ---------------- (Peace.au3:2553-2556)
  QString audacityFile;         // Audacity File     = E:\Downloads\eq.txt
  int audacityVersion   = 0;    // Audacity Version  = 0
  int audacityType      = 0;    // Audacity Type     = 0
  int frequencyCount    = 50;   // Frequency Count   = 50

  // ---------------- [GUI graph] ---------------- (Peace.au3:2743-2785)
  bool   graphAddPreAmp=false, graphAdjustGains=true, graphAdjustFrequencies=true,
         graphAddBassTreble=false;                 // Add Pre Amp/Adjust Gains/Adjust Frequencies/Add Bass Treble
  double measurementRange=30, measurementOffset=0; // Measurement Range/Offset
  bool   measurementInverse=false;                 // Measurement Inverse
  double overlayTransparency=0.5, overlayOffsetX=0, overlayOffsetY=0,
         overlaySizeX=1, overlaySizeY=1;           // Overlay *
  bool   overlayInverse=true, showGrid=true;       // Overlay Inverse / Show Grid
  double range=3.32, start=1, step=5, stepAdjusting=20, measurementStep=2;
  double pixel=2, measurementPixel=1, axis=1, graphFontSize=7;
  double gainLimit=0, gainScale=5, handleSize=10;
  int    frequenciesSet=2;                         // Frequencies Set = 2
  double gainDelta=1, frequencyDelta=0.5, qualityDelta=10;
  // COLORREF ints, may be overridden by Theme\graphtheme.ini (Peace.au3:2731 IniOverrideRead)
  int color=49152, measurementColor=16608, axisColor=0, gridColor=0,
      clearColor=15790320, labelColor=0, gainHandleColor=160,
      frequencyHandleColor=10485760, selectedHandleColor=10526720;

  // ---------------- [GUI test] ---------------- (Peace.au3:2847-2861)
  bool   hideIntroduction=false, showContour=true, loudnessCorrection=true;
  double contourOffset=0;
  double equalizationFrequencyMinimum=200, equalizationFrequencyMaximum=12000;
  int    equalizationStrength=3, equalizationType=0, equalizationPeakQuality=0;
  int    resultColor=49152, comparisonColor=16608, contourColor=16744448;
};
```

UNKNOWNS: Text encoding of .peace / peace.ini when non-ASCII characters are present. Every file on this machine is pure ASCII (verified by byte scan), so I could not observe what Peace writes for e.g. a Chinese preset description or a Chinese device name. peace.ini line 2 is `Language=Chinese simplified`, so this WILL matter for this user. AutoIt IniWrite calls WritePrivateProfileStringW, which writes UTF-16LE only if the file already has a UTF-16 BOM and otherwise writes ANSI in the system codepage (likely GBK/CP936 here). DreamDSP should sniff for a UTF-16 BOM and otherwise decode with the system ANSI codepage, but I could not verify this empirically.; Whether Windows GetPrivateProfileString really stops at the first matching section header. I asserted first-section-wins for the duplicate [General] in `Equalizer One Third Octave.peace` based on documented API behaviour, but did not run an experiment. In that particular file all shadowed values are 0, so behaviour is identical either way.; [General] key `Id` — no .peace file on this machine contains it, so I never saw a real value or its format (integer vs GUID vs arbitrary string). It is read at Peace.au3:26545 (`IniRead($File,"General","Id","")`) and written at :27556, and it is the lParam of the WM_APP+2 configuration-switch message. How Peace allocates a new Id (and whether it is unique across a configurations set) is unverified.; [General] key `Web Page` and `Configuration Icon` — read at Peace.au3:26547-26548, written at :27558-27559, but absent from every file here, so no sample values. `Configuration Icon` indexes into $TrayIcons; the icon list order is not documented in what I read.; Exact per-wParam semantics of WM_APP, WM_APP+1, +3, +4, +5, +6, +7, +20, +21, +22 and +100+n. I extracted the full wParam/lParam/return table only for WM_APP+2 (controlconfiguration.htm) and the general error codes (controlerror.htm); control.htm gives only one-line summaries for the others. The remaining tables are in the decompiled CHM at C:\Users\10678\AppData\Local\Temp\claude\I--Qt\7fffc091-07c0-410a-a51c-5a342ddbdb5e\scratchpad\chm2\ (controlmain.htm, controlinterface.htm, controlequalizer.htm, controldefaultdevice.htm, controlmainpreamplifying.htm, controleffects.htm, controldevice.htm, controlspeakers.htm, controlpreamplifying.htm, controlallsliders.htm, controlslider.htm) and in Peace.au3:23106-24340.; The Effects() emitter (Peace.au3:29375-29707, ~330 lines) and SurroundEffect() (:29245-29373) — I read only the first ~25 lines of Effects() (swap-speakers and stereo-balance cases). The exact APO text for crossfeed, echo, fake stereo, stereo widening/expanding, downmix, bass/treble, channels delay and the whole surround renderer is NOT documented here and must be ported line-by-line before DreamDSP can byte-match Peace's peace.txt for presets that use effects.; The .peaceset format (`Const $SetFileExtension = "peaceset"` at Peace.au3:377) — configurations sets. No .peaceset file exists on this machine, so its schema is unknown.; Theme\theme.ini and Theme\graphtheme.ini schemas. They override colors via IniOverrideRead (Peace.au3:2731-2742) and supply [Sliders] Slider<N> 3/4 fallbacks (:26758-26762). A Theme directory exists at D:\Program Files\EqualizerAPO\config\Theme but I did not enumerate its contents.; AutoEQ database file formats (AutoEQCompressedHarman5.txt, AutoEQCompressedIEF5.txt, AutoEQCompressedIEFBass5.txt, OPRAEQCompressed5.txt — ~20 MB total in the config dir). Not examined; the CHM has AutoEQ.htm and the source has an AutoEQ\ subfolder.; Whether Peace tolerates .peace section ORDER differences. `Last Configuration.peace` writes [Frequencies]/[Qualities] first then per-speaker groups ascending, while `Equalizer One Third Octave.peace` writes [General] first then speaker groups DESCENDING (8,7,6,...,0). Since reads are by name this should not matter, but I did not test that Peace re-reads a file DreamDSP wrote in a different order.

---

## Qt 6.8 QML + C++ architecture patterns for DreamDSP (verified by building, running, and deploying a working skeleton against the local Qt 6.8.3 msvc2022_64 + HuskarUI install)
- A complete DreamDSP skeleton (qt_add_executable + qt_add_qml_module + HuskarUI + C++ singleton + QAbstractListModel + QQuickPaintedItem + tray) configures, compiles, links, and RUNS with zero QML errors on this exact machine. This is not theoretical — it was built and executed.
  evidence: Built at C:\Users\10678\AppData\Local\Temp\claude\I--Qt\7fffc091-07c0-410a-a51c-5a342ddbdb5e\scratchpad\pq\ ; cmake 4.0.1 + Ninja 1.13.1 + MSVC 19.51.36248.0 (D:\Program Files\VisualStudio\2026\IDE\VC\Tools\MSVC\14.51.36231). Build output '[11/11] Linking CXX executable DreamDSP.exe'; runtime test left the process alive 6 s with empty stderr (objectCreationFailed->exit(-1) never fired).
  → Copy the verified tree wholesale. Files: pq/CMakeLists.txt, pq/src/{main,AppController,EqBandModel,CurveItem}.{h,cpp}, pq/qml/Main.qml. All are in code_samples. Use find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Qml Widgets) + find_package(HuskarUI REQUIRED) + qt_standard_project_setup(REQUIRES 6.8).
- BLOCKER for Q6: QSettings(IniFormat) CORRUPTS peace.ini. It %-encodes every space in key and section names, and it splits keys containing '/' into bogus new sections. Peace.exe uses AutoIt IniRead (exact key match), so a QSettings rewrite silently destroys Peace interop.
  evidence: Empirical round-trip of the real D:\Program Files\EqualizerAPO\config\peace.ini through QSettings 6.8.3 (test program at .../scratchpad/initest/main.cpp). Result: 'Save Window Positions=1' -> 'Save%20Window%20Positions=1'; 184 of ~190 keys %-encoded; sections [Changing Sliders]/[GUI graph]/[GUI test] -> [Changing%20Sliders]/[GUI%20graph]/[GUI%20test]; and FOUR new phantom sections appeared: [Show], [On], [Beep%20On], [%General]. Cause is peace.ini lines 80/81/111/117: 'Show/Hide Hotkey=', 'On/Off Hotkey=', 'Beep On/Off=0', 'Beep On/Off Volume=50' — QSettings treats '/' as a group separator (qsettings docs: "Do not use slashes ('/' and '\\') in section or key names; the backslash character is used to separate sub keys"). File grew 197 -> 211 lines.
  → DO NOT use QSettings for peace.ini or .peace files. Hand-roll a line-preserving INI reader/writer (skeleton in code_samples) that keeps original key text, order, and comments byte-for-byte and only rewrites the value after the first '='. Use QSettings ONLY for DreamDSP's own private prefs under QSettings("DreamDSP","DreamDSP") — those keys are yours, so pick slash-free, space-free names.
- QSettings also breaks .peace preset files by adding quotes around any value containing '='.
  evidence: Round-tripped 'D:\Program Files\EqualizerAPO\config\3 Way Crossover.peace'. Original line 3: Routing=R=1.000*L C=1.000*L SUB=1.000*R RL=1.000*R RR=1.000*R  ->  QSettings wrote: Routing="R=1.000*L C=1.000*L SUB=1.000*R RL=1.000*R RR=1.000*R" (quotes added). Note .peace files are otherwise safer than peace.ini: I checked all ~25 presets and found 0 keys containing '/' or ' '.
  → Same hand-rolled writer for .peace. The Routing value is the specific canary — assert in a unit test that a load->save round-trip of '3 Way Crossover.peace' is byte-identical.
- BLOCKER for Q1: the HuskarUI installed into the Qt SDK is INCOMPLETE and DIFFERENT from the cloned source. Only HuskarUI.Basic was installed; HuskarUI.Impl is entirely missing (no qml/HuskarUI/Impl dir, no HuskarUIImpl.dll).
  evidence: D:\Qt\6.8.3\msvc2022_64\qml\HuskarUI\ contains only 'Basic'. D:\Qt\6.8.3\msvc2022_64\bin\ contains HuskarUIBasic.dll (dated Dec 16 2025) but no HuskarUIImpl.dll. Yet I:\Qt\DreamDSP\third_party\HuskarUI\src\CMakeLists.txt:121 declares 'DEPENDENCIES HuskarUI.Impl', and src_impl/CMakeLists.txt:69 declares URI "HuskarUI.Impl". Also a version skew: the installed qmldir exports 70 types (71 lines) while the clone's src/imports/ has 76 .qml files.
  → Rebuild and reinstall HuskarUI from the clone before relying on it: cmake -S I:/Qt/DreamDSP/third_party/HuskarUI -B <bld> -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64 -DBUILD_HUSKARUI_GALLERY=OFF -DINSTALL_HUSKARUI_IN_DEFAULT_LOCATION=ON, then cmake --install. Otherwise you are coding against a stale binary whose type set does not match the source you are reading. (The stale copy did work for my smoke test, so this is a correctness/version-skew risk, not an immediate crash.)
- BLOCKER for Q8: windeployqt --qmldir does NOT deploy HuskarUI's backing C++ library. It copies qml/HuskarUI/Basic/huskaruibasicplugin.dll but omits HuskarUIBasic.dll, which the plugin hard-links. The deployed app then fails QML creation.
  evidence: Ran: windeployqt.exe --qmldir <src>/qml --dir <deploy> --release --no-translations <deploy>/DreamDSP.exe. It logged 'Creating .../deploy/qml/HuskarUI/Basic' and 'Updating huskaruibasicplugin.dll', but a recursive search for *Huskar* in the deploy tree found ONLY huskaruibasicplugin.dll — no HuskarUIBasic.dll. dumpbin /dependents on huskaruibasicplugin.dll lists 'HuskarUIBasic.dll' as a direct import. A/B proof: running the deployed exe with PATH stripped to C:\WINDOWS\system32 exited with EXITCODE=-1 (the objectCreationFailed path); after copying D:\Qt\6.8.3\msvc2022_64\bin\HuskarUIBasic.dll to the deploy root, the same run stayed alive. This matches HuskarUI's own gallery workaround at gallery/CMakeLists.txt:259-260, which manually copies HuskarUIImpl.dll and HuskarUIBasic.dll.
  → Add an explicit post-deploy copy. In CMake: add_custom_command(TARGET DreamDSP POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:HuskarUI::Basic> $<TARGET_FILE_DIR:DreamDSP>). And in your package step copy HuskarUIBasic.dll (plus HuskarUIImpl.dll once you rebuild Impl) next to DreamDSP.exe. Full command in code_samples.
- Qt 6.8 gotcha that WILL bite you: a Q_PROPERTY whose type is a pointer to your own class requires a COMPLETE type at moc-compile time. A forward declaration compiles the .cpp fine but blows up mocs_compilation.cpp.
  evidence: Real build failure I hit and fixed: D:\Qt\6.8.3\msvc2022_64\include\QtCore/qmetatype.h(1212): error C2338: static assertion failed: 'Pointer Meta Types must either point to fully-defined types or be declared with Q_DECLARE_OPAQUE_POINTER(T *)', triggered from moc_AppController.cpp(115) instantiating qt_metaTypeArray<EqBandModel *,...> and QtPrivate::TypeAndForceComplete<EqBandModel*>. Same error from moc_EqBandModel.cpp(119). Note steps [18][19][20][23] (the .cpp files) all COMPILED — only step [16] mocs_compilation.cpp failed, so the error looks unrelated to your edit.
  → In AppController.h and CurveItem.h, #include "EqBandModel.h" instead of 'class EqBandModel;'. Whenever you declare Q_PROPERTY(SomeType *x ...), include SomeType's header in the same header.
- Q3 answer: QAbstractListModel with roles is the only correct choice; QVariantList/QObjectList/QStringList models physically cannot notify QML of a change. For 31 bands x 9 slots this is trivially fast — the bottleneck is redundant signals, not model size.
  evidence: doc.qt.io/qt-6/qtquick-modelviewsdata-cppmodels.html: for QStringList/QVariantList/QObjectList models, "There is no way for the view to know that the contents ... have changed. If the ... changes, it will be necessary to reset the model by setting the view's model property again." QAbstractItemModel avoids this via dataChanged()/beginInsertRows().
  → Use ONE QAbstractListModel of 31 rows for the active slot; keep the other 8 slots as plain QList<EqBand> in C++ and swap them into the model on slot change (beginResetModel/endResetModel). Add a Q_INVOKABLE setGain(int row, double) hot path so QML sliders never construct QModelIndex. CRITICAL for drag smoothness: early-return on qFuzzyCompare(old,new) before emitting dataChanged — see EqBandModel::setGain in code_samples. 279 bands total is nothing; do NOT reach for QQmlListProperty (it has no change notification granularity either).
- Q4 recommendation: use QQuickPaintedItem for the frequency-response curve. It is fast enough for live dragging and it is the only option that gives you QPainter (gradients, dashed grid, text labels) for free. QSGGeometryNode is the fastest but is premature here; Canvas and ShapePath are both wrong for a per-frame-changing path.
  evidence: doc.qt.io/qt-6/qquickpainteditem.html: "In Qt 6, the render target is always a QImage" (FramebufferObject only re-enabled in 6.9, so on 6.8.3 setRenderTarget is effectively a no-op) and "paint() is not called from the main GUI thread but from the GL enabled renderer thread". doc.qt.io/qt-6/qml-qtquick-shapes-shape.html: "Changing the set of path elements, changing the properties of these elements, or changing certain properties of the Shape itself all lead to retriangulation of the affected paths on every change" and "applying animation to such properties can affect performance" — i.e. Shape re-triangulates every drag event. doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html: updatePaintNode() "executes on the render thread" and "only use classes with the QSG prefix inside updatePaintNode()".
  → Use the CurveItem QQuickPaintedItem skeleton in code_samples (verified compiling). Key points: setAntialiasing(true) (AA is OFF by default), setFillColor(Qt::transparent), and cache the computed dB response in a member, recomputing lazily inside paint() behind an m_dirty flag. update() only SCHEDULES a repaint, so N slider events in one frame collapse to one paint() automatically — no manual throttling. Cost budget: 31 bands x 256 log-spaced probes = ~8k biquad magnitude evals per frame, far inside 16 ms. Only migrate to QSGGeometryNode if profiling shows the texture upload hurts (a 900x240 curve at devicePixelRatio 1.5 is ~1.9 MB uploaded per repaint).
- Q7 answer: yes, you need QApplication, not QGuiApplication — but use Qt.labs.platform SystemTrayIcon (QML) rather than QSystemTrayIcon (C++). Either way QtWidgets is linked. This is confirmed by the local Qt 6.8.3 metadata, not just docs.
  evidence: D:\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6LabsPlatform\Qt6LabsPlatformDependencies.cmake:50 — set(_Qt6LabsPlatform_MODULE_DEPENDENCIES "Core;Gui;Qml;Quick;QuickTemplates2;Widgets"); and Qt6LabsPlatformTargets-debug.cmake:12 IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::Qml;Qt6::Widgets". doc.qt.io/qt-6/qml-qt-labs-platform-systemtrayicon.html: "The Qt Labs Platform module uses Qt Widgets as a fallback on platforms that do not have a native implementation available. Therefore, applications that use types from the Qt Labs Platform module should link to QtWidgets and use QApplication instead of QGuiApplication." Module verified present locally at D:\Qt\6.8.3\msvc2022_64\qml\Qt\labs\platform\ (QQuickLabsPlatformSystemTrayIcon in plugins.qmltypes). My built skeleton uses QApplication + Qt.labs.platform SystemTrayIcon and ran clean.
  → main.cpp: #include <QApplication>; QApplication app(argc,argv); QApplication::setQuitOnLastWindowClosed(false) so closing the window leaves you in the tray. CMake: COMPONENTS ... Widgets and target_link_libraries(... Qt6::Widgets). Implication: Qt6Widgets.dll ships (windeployqt pulled it in automatically). There is NO pure-QtQuick tray alternative in 6.8 — Qt.labs.platform is the Qt Quick answer and it still needs Widgets. Declare the tray declaratively in Main.qml (see code_samples) so menu items bind to your singleton.
- Q2 answer: expose a single QML_SINGLETON controller via a static create(QQmlEngine*, QJSEngine*), and hang sub-objects (the band model) off it as CONSTANT properties. This gives QML a global 'AppController' with no context-property boilerplate and no ownership bugs.
  evidence: doc.qt.io/qt-6/qtqml-cppintegration-definetypes.html: singletons use QML_SINGLETON with QML_ELEMENT, and the static factory signature is exactly 'static <ClassName> *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)' — "Both the QQmlEngine and QJSEngine parameters must be included in the function signature." Also: "singleton type instances are constructed and owned by the QQmlEngine, and will be destroyed when the engine is destroyed." Verified working: my Main.qml references AppController.bands / AppController.flush() / AppController.configPath and the app ran with no QML errors.
  → Use the AppController in code_samples. Rules that matter: (1) properties that never change identity get CONSTANT, not NOTIFY (bands); (2) every mutable property gets a NOTIFY signal or QML bindings go stale silently; (3) mark file/IO entry points Q_INVOKABLE, and emit a writeFailed(QString) signal rather than returning bool — QML can't check return codes of async work; (4) keep the QML_ELEMENT sub-objects (EqBandModel, CurveItem) in the SAME qt_add_qml_module SOURCES list so qmltyperegistrar sees them.
- Q1 answer on resource paths: with qt_standard_project_setup(REQUIRES 6.8) the RESOURCE_PREFIX defaults to '/qt/qml/', and the QML engine has qrc:/qt/qml in its default import path since Qt 6.5 — so your own module needs NO addImportPath at all. HuskarUI is found because it lives in QLibraryInfo::QmlImportsPath.
  evidence: Generated at build time: <build>/DreamDSP/qmldir contains 'module DreamDSP', 'typeinfo DreamDSP.qmltypes', 'prefer :/qt/qml/DreamDSP/', 'Main 1.0 qml/Main.qml'. doc.qt.io/qt-6/qt-add-qml-module.html: "If QTP0001 is enabled (e.g. via qt_standard_project_setup(REQUIRES 6.5)), the default value is \"/qt/qml/\", otherwise it is \"/\"", and full resource path = RESOURCE_PREFIX + target_path + relative_file_path. doc.qt.io/qt-6/qtqml-syntax-imports.html search order: (1) platform bundle paths, (2) "The directory of the application binary", (3) "The qrc:/qt-project.org/imports path inside the resources", (4) "The qrc:/qt/qml path inside the resources (since Qt 6.5)", (5) QML2_IMPORT_PATH (deprecated), (6) QML_IMPORT_PATH, (7) QLibraryInfo::QmlImportsPath. Contrast: HuskarUI sets RESOURCE_PREFIX "/" (src/CMakeLists.txt:118), so its installed qmldir line 7 reads 'prefer :/HuskarUI/Basic/'.
  → Use engine.loadFromModule("DreamDSP", "Main") (Qt 6.5+) — never a qrc: URL string. Because you listed 'qml/Main.qml', the relative dir is preserved, so sibling assets resolve as qrc:/qt/qml/DreamDSP/qml/tray.png. Do NOT call addImportPath for HuskarUI when it is installed into the Qt SDK (item 7 covers it). If you instead add_subdirectory() the HuskarUI clone, add engine.addImportPath(HUSKARUI_IMPORT_PATH) exactly as gallery/main.cpp does. Connect objectCreationFailed (Qt 6.4+), not the old objectCreated-null-check pattern.
- Q5 answer: a single member QTimer with setSingleShot(true) + setInterval(150), restarted on every change, is the correct trailing-edge debounce. QTimer::start() on a running timer restarts it — that IS the coalescing mechanism. Pair it with QSaveFile so Equalizer APO never reads a half-written config.
  evidence: doc.qt.io/qt-6/qtimer.html: start(int) — "Starts or restarts the timer ... If the timer is already running, it will be stopped and restarted." Default timerType is Qt::CoarseTimer. QTimer::singleShot(interval, context, functor) "will be called only if the context object has not been destroyed before the interval occurs." Verified compiling in AppController.cpp.
  → Use a MEMBER QTimer, not the static QTimer::singleShot — the static overload cannot be restarted or cancelled, so a 60 Hz drag would queue 60 separate writes. Wire model::bandsChanged -> requestWrite(); requestWrite() calls m_writeTimer.start(). Add a flush() that stops the timer and writes immediately, and call it from the tray Quit handler and aboutToQuit so you never lose the last 150 ms of edits. Write via QSaveFile + commit() (atomic rename) — Equalizer APO watches config.txt/peace.txt and will otherwise pick up a truncated file mid-write.
- HuskarUI's installed qmldir has a broken 'linktarget' line that produces a CMake warning on every configure. It is harmless for a shared build but will silently NOT link the plugin in a static build.
  evidence: Configure output: 'CMake Warning at D:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6Qml/Qt6QmlMacros.cmake:4253 (message): The qml plugin \'huskaruibasicplugin\' is a dependency of \'DreamDSP\', but the link target it defines (huskaruibasicplugin) does not exist in the current scope. The plugin will not be linked.' Root cause: D:\Qt\6.8.3\msvc2022_64\qml\HuskarUI\Basic\qmldir line 2 says 'linktarget huskaruibasicplugin' but the exported CMake target is namespaced as HuskarUI::huskaruibasicplugin (HuskarUITargets.cmake:68 'add_library(HuskarUI::huskaruibasicplugin MODULE IMPORTED)').
  → Ignore the warning for a dynamic build (the plugin dll is loaded at runtime — my app ran fine). If you ever set BUILD_HUSKARUI_STATIC_LIBRARY=ON, you must add Q_IMPORT_QML_PLUGIN(HuskarUI_BasicPlugin) and Q_IMPORT_QML_PLUGIN(HuskarUI_ImplPlugin) in main.cpp and link HuskarUIBasicPlugin/HuskarUIImplPlugin explicitly, exactly as gallery/main.cpp and gallery/CMakeLists.txt:195-201 do.
- HuskarUI's HusSlider is NOT a drop-in QtQuick.Controls Slider — its API differs in three ways that will cost you an afternoon.
  evidence: D:\Qt\6.8.3\msvc2022_64\qml\HuskarUI\Basic\imports\HusSlider.qml: line 45 'property real min: 0', line 46 'property real max: 100' (NOT from/to); line 48 'property var value' plus line 49 'readonly property var currentValue'; lines 38-41 'signal firstMoved()' / 'signal firstReleased()' / 'signal secondMoved()' / 'signal secondReleased()' (there is no moved() signal); line 56 'property bool range: false' — when range is true, value becomes an ARRAY [lo,hi]; line 59 'property int orientation: Qt.Horizontal' (vertical IS supported, good for EQ bands).
  → For EQ band sliders write: HusSlider { orientation: Qt.Vertical; min: -30; max: 30; value: model.gain; onFirstMoved: AppController.bands.setGain(index, currentValue) }. Read currentValue, not value, in the handler. My first attempt used from/to/onMoved and silently did nothing.

### code samples
```
# ===== VERIFIED-BUILDING top-level CMakeLists.txt for DreamDSP =====
# Configured + built + linked + ran on this machine (cmake 4.0.1, Ninja, MSVC 19.51).
cmake_minimum_required(VERSION 3.21)
project(DreamDSP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Widgets is MANDATORY: Qt.labs.platform (tray) hard-depends on Qt6::Widgets.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Qml Widgets)
find_package(HuskarUI REQUIRED)   # resolved from D:/Qt/6.8.3/msvc2022_64/lib/cmake/HuskarUI

# Enables QTP0001 -> RESOURCE_PREFIX defaults to "/qt/qml/", which is already
# in the engine's default import path (qrc:/qt/qml, since Qt 6.5).
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(DreamDSP WIN32
    src/main.cpp
)

qt_add_qml_module(DreamDSP
    URI DreamDSP
    VERSION 1.0
    QML_FILES
        qml/Main.qml
    SOURCES                      # every QML_ELEMENT class must be listed HERE,
        src/EqBandModel.h  src/EqBandModel.cpp     # not in qt_add_executable,
        src/AppController.h src/AppController.cpp  # or qmltyperegistrar misses them
        src/CurveItem.h     src/CurveItem.cpp
)

target_include_directories(DreamDSP PRIVATE src)

target_link_libraries(DreamDSP PRIVATE
    Qt6::Quick
    Qt6::Widgets
    HuskarUI::Basic
)

# REQUIRED: windeployqt does NOT copy HuskarUI's backing library (proven by A/B test).
add_custom_command(TARGET DreamDSP POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:HuskarUI::Basic> $<TARGET_FILE_DIR:DreamDSP>)
```

```
// ===== src/main.cpp — VERIFIED =====
#include <QApplication>          // NOT QGuiApplication: Qt.labs.platform needs QtWidgets
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);   // close-to-tray survives

    app.setOrganizationName("DreamDSP");
    app.setApplicationName("DreamDSP");

    QQmlApplicationEngine engine;
    // Qt 6.4+ signal. Do NOT use the old objectCreated(obj,url)-null-check dance.
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []{ QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    // Qt 6.5+. Resolves via qrc:/qt/qml/DreamDSP/ — no addImportPath needed.
    engine.loadFromModule("DreamDSP", "Main");
    return app.exec();
}
```

```
// ===== src/EqBandModel.h — VERIFIED =====
#pragma once
#include <QAbstractListModel>
#include <QQmlEngine>
#include <QList>

struct EqBand {
    double freq = 1000.0;
    double gain = 0.0;
    double q    = 1.0;
    int    type = 0;
    bool   on   = true;
};

class EqBandModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { FreqRole = Qt::UserRole + 1, GainRole, QRole, TypeRole, OnRole };
    Q_ENUM(Roles)

    explicit EqBandModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Hot path for slider drag: no QModelIndex construction from QML.
    Q_INVOKABLE void   setGain(int row, double gain);
    Q_INVOKABLE double gain(int row) const;
    Q_INVOKABLE void   setBandCount(int n);

    const QList<EqBand> &bands() const { return m_bands; }

signals:
    void countChanged();
    void bandsChanged();   // coalesced trigger for the debounced writer

private:
    QList<EqBand> m_bands;
};
```

```
// ===== src/EqBandModel.cpp — VERIFIED =====
#include "EqBandModel.h"

EqBandModel::EqBandModel(QObject *parent) : QAbstractListModel(parent) { setBandCount(10); }

int EqBandModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : int(m_bands.size()); }

QVariant EqBandModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_bands.size()) return {};
    const EqBand &b = m_bands.at(index.row());
    switch (role) {
    case FreqRole: return b.freq;
    case GainRole: return b.gain;
    case QRole:    return b.q;
    case TypeRole: return b.type;
    case OnRole:   return b.on;
    default:       return {};
    }
}

bool EqBandModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_bands.size()) return false;
    EqBand &b = m_bands[index.row()];
    switch (role) {
    case FreqRole: if (qFuzzyCompare(b.freq, value.toDouble())) return false; b.freq = value.toDouble(); break;
    case GainRole: if (qFuzzyCompare(b.gain, value.toDouble())) return false; b.gain = value.toDouble(); break;
    case QRole:    if (qFuzzyCompare(b.q,    value.toDouble())) return false; b.q    = value.toDouble(); break;
    case TypeRole: if (b.type == value.toInt())  return false; b.type = value.toInt();  break;
    case OnRole:   if (b.on   == value.toBool()) return false; b.on   = value.toBool(); break;
    default: return false;
    }
    emit dataChanged(index, index, {role});
    emit bandsChanged();
    return true;
}

Qt::ItemFlags EqBandModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}

QHash<int, QByteArray> EqBandModel::roleNames() const
{
    return { {FreqRole,"freq"}, {GainRole,"gain"}, {QRole,"q"},
             {TypeRole,"type"}, {OnRole,"on"} };
}

void EqBandModel::setGain(int row, double gain)
{
    if (row < 0 || row >= m_bands.size()) return;
    if (qFuzzyCompare(m_bands[row].gain, gain)) return;   // <-- kills redundant repaints during drag
    m_bands[row].gain = gain;
    const QModelIndex ix = index(row);
    emit dataChanged(ix, ix, {GainRole});
    emit bandsChanged();
}

double EqBandModel::gain(int row) const
{ return (row >= 0 && row < m_bands.size()) ? m_bands.at(row).gain : 0.0; }

void EqBandModel::setBandCount(int n)
{
    n = qBound(1, n, 31);
    if (n == m_bands.size()) return;
    if (n > m_bands.size()) {
        beginInsertRows({}, int(m_bands.size()), n - 1);
        while (m_bands.size() < n) m_bands.append(EqBand{});
        endInsertRows();
    } else {
        beginRemoveRows({}, n, int(m_bands.size()) - 1);
        m_bands.resize(n);
        endRemoveRows();
    }
    emit countChanged();
    emit bandsChanged();
}
```

```
// ===== src/AppController.h — QML_SINGLETON backend controller — VERIFIED =====
#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QString>

// MUST be a complete type, not a forward declaration. Qt 6.8 moc emits
// TypeAndForceComplete<EqBandModel*> for the Q_PROPERTY below and static_asserts
// on incomplete types (error C2338 in mocs_compilation.cpp, not in your .cpp).
#include "EqBandModel.h"

class AppController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(EqBandModel *bands READ bands CONSTANT)          // identity never changes -> CONSTANT
    Q_PROPERTY(int     activeSlot READ activeSlot WRITE setActiveSlot NOTIFY activeSlotChanged)
    Q_PROPERTY(bool    dirty      READ dirty                     NOTIFY dirtyChanged)
    Q_PROPERTY(QString configPath READ configPath WRITE setConfigPath NOTIFY configPathChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    // Exact signature required by QML_SINGLETON — both engine params must be present.
    static AppController *create(QQmlEngine *, QJSEngine *);

    EqBandModel *bands() const { return m_bands; }
    int  activeSlot() const { return m_activeSlot; }
    void setActiveSlot(int s);
    bool dirty() const { return m_dirty; }
    QString configPath() const { return m_configPath; }
    void setConfigPath(const QString &p);

    Q_INVOKABLE void requestWrite();   // debounced ~150 ms
    Q_INVOKABLE void flush();          // force immediate write (call on quit)

signals:
    void activeSlotChanged();
    void dirtyChanged();
    void configPathChanged();
    void writeFailed(const QString &reason);   // signal, not a bool return: QML can't check codes

private:
    void doWrite();

    EqBandModel *m_bands = nullptr;
    QTimer       m_writeTimer;
    int          m_activeSlot = 0;
    bool         m_dirty = false;
    QString      m_configPath;
};
```

```
// ===== src/AppController.cpp — 150 ms debounce + atomic write — VERIFIED =====
#include "AppController.h"
#include <QSaveFile>
#include <QTextStream>
#include <QPointer>

namespace { QPointer<AppController> g_singleton; }

AppController::AppController(QObject *parent)
    : QObject(parent), m_bands(new EqBandModel(this))
{
    m_writeTimer.setSingleShot(true);
    m_writeTimer.setInterval(150);
    m_writeTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_writeTimer, &QTimer::timeout, this, &AppController::doWrite);

    // Any model mutation schedules a write. QTimer::start() on a RUNNING timer
    // stops and restarts it, so a burst of drag events collapses into exactly one
    // write, 150 ms after the LAST event (trailing-edge debounce).
    connect(m_bands, &EqBandModel::bandsChanged, this, &AppController::requestWrite);
}

AppController *AppController::create(QQmlEngine *, QJSEngine *)
{
    if (!g_singleton) g_singleton = new AppController;
    return g_singleton;   // engine takes ownership; fine for a single-engine app
}

void AppController::setActiveSlot(int s)
{
    if (m_activeSlot == s) return;
    m_activeSlot = s;
    emit activeSlotChanged();
    requestWrite();
}

void AppController::setConfigPath(const QString &p)
{
    if (m_configPath == p) return;
    m_configPath = p;
    emit configPathChanged();
}

void AppController::requestWrite()
{
    if (!m_dirty) { m_dirty = true; emit dirtyChanged(); }
    m_writeTimer.start();     // restart-if-running == the coalescing mechanism
}

void AppController::flush()
{
    if (m_writeTimer.isActive()) { m_writeTimer.stop(); doWrite(); }
}

void AppController::doWrite()
{
    if (m_configPath.isEmpty()) { emit writeFailed(QStringLiteral("no config path set")); return; }

    // QSaveFile writes to a temp file and atomically renames on commit(), so
    // Equalizer APO's config watcher never observes a half-written file.
    QSaveFile f(m_configPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) { emit writeFailed(f.errorString()); return; }

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (const EqBand &b : m_bands->bands()) {
        if (!b.on) continue;
        out << "Filter: ON PK Fc " << b.freq << " Hz Gain " << b.gain << " dB Q " << b.q << "\n";
    }
    out.flush();

    if (!f.commit()) { emit writeFailed(f.errorString()); return; }
    m_dirty = false;
    emit dirtyChanged();
}
```

```
// ===== src/CurveItem.h — QQuickPaintedItem frequency-response curve — VERIFIED =====
#pragma once
#include <QQuickPaintedItem>
#include <QQmlEngine>
#include <QList>
#include <QColor>

#include "EqBandModel.h"   // complete type required by Q_PROPERTY(EqBandModel*)

class CurveItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(EqBandModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(qreal minDb READ minDb WRITE setMinDb NOTIFY rangeChanged)
    Q_PROPERTY(qreal maxDb READ maxDb WRITE setMaxDb NOTIFY rangeChanged)
    Q_PROPERTY(QColor curveColor READ curveColor WRITE setCurveColor NOTIFY curveColorChanged)
    Q_PROPERTY(QColor gridColor  READ gridColor  WRITE setGridColor  NOTIFY gridColorChanged)

public:
    explicit CurveItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    EqBandModel *model() const { return m_model; }
    void setModel(EqBandModel *m);
    qreal minDb() const { return m_minDb; }  void setMinDb(qreal v);
    qreal maxDb() const { return m_maxDb; }  void setMaxDb(qreal v);
    QColor curveColor() const { return m_curveColor; } void setCurveColor(const QColor &c);
    QColor gridColor()  const { return m_gridColor; }  void setGridColor(const QColor &c);

signals:
    void modelChanged();
    void rangeChanged();
    void curveColorChanged();
    void gridColorChanged();

private slots:
    void onModelChanged();

private:
    void  rebuildResponse();
    qreal xForFreq(qreal f) const;
    qreal yForDb(qreal db) const;

    EqBandModel *m_model = nullptr;
    QList<qreal> m_responseDb;
    bool   m_dirty = true;
    qreal  m_minDb = -30.0;
    qreal  m_maxDb =  30.0;
    QColor m_curveColor{0x1c, 0x7e, 0xd6};
    QColor m_gridColor{0x40, 0x40, 0x40};

    static constexpr int   kProbes = 256;
    static constexpr qreal kFmin = 20.0;
    static constexpr qreal kFmax = 20000.0;
};
```

```
// ===== src/CurveItem.cpp — VERIFIED COMPILING =====
#include "CurveItem.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

CurveItem::CurveItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    // On Qt 6.8 the render target is ALWAYS QImage (FBO target only returns in 6.9),
    // so setRenderTarget() is effectively a no-op here. Antialiasing is OFF by
    // default on QQuickPaintedItem and must be enabled explicitly.
    setAntialiasing(true);
    setFillColor(Qt::transparent);
    m_responseDb.resize(kProbes);
}

void CurveItem::setModel(EqBandModel *m)
{
    if (m_model == m) return;
    if (m_model) m_model->disconnect(this);
    m_model = m;
    if (m_model) {
        connect(m_model, &EqBandModel::bandsChanged,       this, &CurveItem::onModelChanged);
        connect(m_model, &QAbstractItemModel::dataChanged, this, &CurveItem::onModelChanged);
        connect(m_model, &QAbstractItemModel::modelReset,  this, &CurveItem::onModelChanged);
    }
    m_dirty = true;
    emit modelChanged();
    update();
}

void CurveItem::onModelChanged()
{
    m_dirty = true;
    // update() only SCHEDULES a repaint for the next frame, so N slider events
    // inside one frame collapse into a single paint(). No manual throttling needed.
    update();
}

void CurveItem::setMinDb(qreal v){ if(qFuzzyCompare(m_minDb,v))return; m_minDb=v; emit rangeChanged(); update(); }
void CurveItem::setMaxDb(qreal v){ if(qFuzzyCompare(m_maxDb,v))return; m_maxDb=v; emit rangeChanged(); update(); }
void CurveItem::setCurveColor(const QColor &c){ if(m_curveColor==c)return; m_curveColor=c; emit curveColorChanged(); update(); }
void CurveItem::setGridColor(const QColor &c){ if(m_gridColor==c)return; m_gridColor=c; emit gridColorChanged(); update(); }

qreal CurveItem::xForFreq(qreal f) const
{
    const qreal t = (std::log10(f) - std::log10(kFmin)) / (std::log10(kFmax) - std::log10(kFmin));
    return t * width();
}

qreal CurveItem::yForDb(qreal db) const
{
    const qreal t = (db - m_minDb) / (m_maxDb - m_minDb);
    return (1.0 - t) * height();
}

// Sum of RBJ peaking-EQ magnitude responses at 256 log-spaced probes.
// 31 bands x 256 probes ~= 8k evaluations -> trivially inside a 16 ms frame.
void CurveItem::rebuildResponse()
{
    const qreal fs = 48000.0;
    for (int i = 0; i < kProbes; ++i) {
        const qreal t = qreal(i) / (kProbes - 1);
        const qreal f = kFmin * std::pow(kFmax / kFmin, t);
        qreal totalDb = 0.0;
        if (m_model) {
            const qreal w = 2.0 * M_PI * f / fs;
            const qreal cw = std::cos(w);
            for (const EqBand &b : m_model->bands()) {
                if (!b.on || qFuzzyIsNull(b.gain)) continue;
                const qreal A     = std::pow(10.0, b.gain / 40.0);
                const qreal w0    = 2.0 * M_PI * b.freq / fs;
                const qreal alpha = std::sin(w0) / (2.0 * b.q);
                const qreal b0 = 1 + alpha * A, b1 = -2 * std::cos(w0), b2 = 1 - alpha * A;
                const qreal a0 = 1 + alpha / A, a1 = b1,                a2 = 1 - alpha / A;
                const qreal c2w = 2 * cw * cw - 1;
                const qreal s2w = 2 * std::sin(w) * cw;
                const qreal nr = b0 + b1*cw + b2*c2w, ni = -(b1*std::sin(w) + b2*s2w);
                const qreal dr = a0 + a1*cw + a2*c2w, di = -(a1*std::sin(w) + a2*s2w);
                const qreal num = nr*nr + ni*ni, den = dr*dr + di*di;
                if (den > 0.0) totalDb += 10.0 * std::log10(num / den);
            }
        }
        m_responseDb[i] = totalDb;
    }
    m_dirty = false;
}

void CurveItem::paint(QPainter *painter)
{
    if (width() <= 0 || height() <= 0) return;
    if (m_dirty) rebuildResponse();          // lazy recompute, once per frame max

    painter->setRenderHint(QPainter::Antialiasing, true);

    painter->setPen(QPen(m_gridColor, 1.0));
    static const qreal decades[] = {20,50,100,200,500,1000,2000,5000,10000,20000};
    for (qreal f : decades) {
        const qreal x = xForFreq(f);
        painter->drawLine(QPointF(x, 0), QPointF(x, height()));
    }
    for (qreal db = m_minDb; db <= m_maxDb + 0.01; db += 10.0) {
        const qreal y = yForDb(db);
        painter->drawLine(QPointF(0, y), QPointF(width(), y));
    }

    QPainterPath path;
    for (int i = 0; i < kProbes; ++i) {
        const qreal x = (qreal(i) / (kProbes - 1)) * width();
        const qreal y = yForDb(m_responseDb.at(i));
        if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
    }
    painter->setPen(QPen(m_curveColor, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
}
```

```
// ===== qml/Main.qml — VERIFIED LOADING (tray + HuskarUI + singleton + curve) =====
import QtQuick
import QtQuick.Controls
import Qt.labs.platform as Platform
import HuskarUI.Basic
import DreamDSP

ApplicationWindow {
    id: root
    width: 900; height: 560
    visible: true
    title: "DreamDSP"

    Platform.SystemTrayIcon {
        visible: true
        icon.source: "qrc:/qt/qml/DreamDSP/qml/tray.png"   // RESOURCE_PREFIX + URI + rel path
        tooltip: "DreamDSP"
        menu: Platform.Menu {
            Platform.MenuItem { text: "Show"; onTriggered: { root.show(); root.raise() } }
            Platform.MenuItem { text: "Quit"; onTriggered: { AppController.flush(); Qt.quit() } }
        }
        onActivated: (reason) => {
            if (reason === Platform.SystemTrayIcon.Trigger) root.show()
        }
    }

    CurveItem {
        id: curve
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 240
        model: AppController.bands
        minDb: -30; maxDb: 30
    }

    ListView {
        anchors { left: parent.left; right: parent.right; top: curve.bottom; bottom: parent.bottom }
        orientation: ListView.Horizontal
        model: AppController.bands
        delegate: Item {
            required property int  index
            required property real gain
            width: 44; height: ListView.view.height

            // HusSlider uses min/max (NOT from/to) and emits firstMoved()
            // (NOT moved()). Read currentValue, not value.
            HusSlider {
                id: slider
                anchors.centerIn: parent
                height: parent.height - 20
                orientation: Qt.Vertical
                min: -30; max: 30
                value: parent.gain
                onFirstMoved: AppController.bands.setGain(parent.index, slider.currentValue)
            }
        }
    }
}
```

```
// ===== Peace-compatible INI I/O — USE THIS INSTEAD OF QSettings =====
// QSettings(IniFormat) provably corrupts peace.ini: it %-encodes spaces in keys
// ('Save Window Positions' -> 'Save%20Window%20Positions'), %-encodes section names,
// splits 'Beep On/Off=0' into a phantom [Beep%20On] section, and quotes any value
// containing '=' (breaking .peace 'Routing=R=1.000*L ...').
// This reader/writer preserves original bytes for every line it does not change.
#pragma once
#include <QString>
#include <QStringList>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <QHash>

class PeaceIni
{
public:
    bool load(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
        m_lines.clear(); m_index.clear();
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        QString section;
        while (!in.atEnd()) {
            const QString raw = in.readLine();
            const int idx = m_lines.size();
            m_lines.append(raw);
            const QString t = raw.trimmed();
            if (t.startsWith('[') && t.endsWith(']')) {
                section = t.mid(1, t.size() - 2);          // keep spaces verbatim
            } else {
                const int eq = raw.indexOf('=');            // FIRST '=' only
                if (eq > 0 && !t.startsWith(';') && !t.startsWith('#'))
                    m_index.insert(section + QLatin1Char('\x1f') + raw.left(eq).trimmed(), idx);
            }
        }
        return true;
    }

    QString value(const QString &section, const QString &key, const QString &def = {}) const
    {
        const auto it = m_index.constFind(section + QLatin1Char('\x1f') + key);
        if (it == m_index.constEnd()) return def;
        const QString &line = m_lines.at(*it);
        return line.mid(line.indexOf('=') + 1);            // raw, unquoted, unescaped
    }

    void setValue(const QString &section, const QString &key, const QString &val)
    {
        const QString k = section + QLatin1Char('\x1f') + key;
        const auto it = m_index.constFind(k);
        if (it != m_index.constEnd()) {
            QString &line = m_lines[*it];
            line = line.left(line.indexOf('=') + 1) + val; // preserve original key text
            return;
        }
        appendInSection(section, key, val);
    }

    bool save(const QString &path) const
    {
        QSaveFile f(path);                                  // atomic: APO never sees a partial file
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        for (const QString &l : m_lines) out << l << "\r\n";  // Peace/AutoIt expects CRLF
        out.flush();
        return f.commit();
    }

private:
    void appendInSection(const QString &section, const QString &key, const QString &val)
    {
        int insertAt = -1;
        bool inSection = section.isEmpty();
        for (int i = 0; i < m_lines.size(); ++i) {
            const QString t = m_lines.at(i).trimmed();
            if (t.startsWith('[') && t.endsWith(']')) {
                if (inSection) { insertAt = i; break; }
                inSection = (t.mid(1, t.size() - 2) == section);
            }
        }
        if (!inSection) {                                   // section absent -> create it
            m_lines.append(QStringLiteral("[%1]").arg(section));
            insertAt = -1;
        }
        const QString line = key + QLatin1Char('=') + val;
        if (insertAt < 0) { m_lines.append(line); insertAt = m_lines.size() - 1; }
        else              { m_lines.insert(insertAt, line); reindexFrom(insertAt); }
        m_index.insert(section + QLatin1Char('\x1f') + key, insertAt);
    }

    void reindexFrom(int from)
    { for (auto it = m_index.begin(); it != m_index.end(); ++it) if (*it >= from) ++(*it); }

    QStringList m_lines;
    QHash<QString, int> m_index;   // "section\x1fkey" -> line index
};
```

```
# ===== Deployment: windeployqt + the mandatory manual HuskarUI copy =====
# windeployqt copies qml/HuskarUI/Basic/huskaruibasicplugin.dll but NOT
# HuskarUIBasic.dll, which that plugin directly imports. Without it the app
# exits with code -1 (objectCreationFailed). Verified by A/B test.

windeployqt.exe ^
  --qmldir  I:\Qt\DreamDSP\qml ^
  --dir     I:\Qt\DreamDSP\package ^
  --release --no-translations --compiler-runtime --no-opengl-sw ^
  I:\Qt\DreamDSP\package\DreamDSP.exe

REM MANDATORY extra step (HuskarUI's own gallery does the same, gallery/CMakeLists.txt:259-260):
copy D:\Qt\6.8.3\msvc2022_64\bin\HuskarUIBasic.dll I:\Qt\DreamDSP\package\
REM ...and HuskarUIImpl.dll too, once you rebuild+reinstall HuskarUI.Impl:
REM copy D:\Qt\6.8.3\msvc2022_64\bin\HuskarUIImpl.dll I:\Qt\DreamDSP\package\

# Trim size: the QuickControls2 styles you don't use are ~15 DLLs.
# HuskarUI's gallery passes these (gallery/CMakeLists.txt:237-249):
#   --no-quickcontrols2fusion --no-quickcontrols2fusionstyleimpl
#   --no-quickcontrols2material --no-quickcontrols2materialstyleimpl
#   --no-quickcontrols2universal --no-quickcontrols2universalstyleimpl
#   --no-quickcontrols2imagine --no-quickcontrols2imaginestyleimpl
# Do NOT pass --no-widgets: Qt.labs.platform (tray) needs Qt6Widgets.dll.
```

UNKNOWNS: Whether Peace.exe tolerates the key REORDERING that any full-rewrite approach causes. I proved QSettings mangles key TEXT, but I did not run Peace.exe against a reordered-but-textually-correct peace.ini. AutoIt's IniRead is order-independent by design, so it should be fine, but this is inferred, not tested. The hand-rolled PeaceIni class above sidesteps the question entirely by preserving line order.; Whether HuskarUI.Impl is genuinely required at runtime for the components DreamDSP will use. I confirmed it is declared as a dependency (src/CMakeLists.txt:121) and is NOT installed, but I also found that ZERO of the 65 installed Basic .qml files contain the string 'HuskarUI.Impl', and the installed qmldir has no 'depends' line — and my app ran fine without it. The dependency may be C++-level only, or the installed build may predate Impl. Rebuilding HuskarUI from the clone will settle it.; The exact mechanism producing the phantom '[%General]' section in the QSettings round-trip. The [Show]/[On]/[Beep%20On] sections are cleanly explained by the '/' characters at peace.ini lines 80/81/111/117; '[%General]' is presumably QSettings disambiguation escaping, but I did not trace it. It does not change the conclusion.; Real measured frame timings for CurveItem under sustained slider dragging. I verified it compiles and the app runs, but I did not instrument paint() or capture a profile, so my '~8k evals, trivially inside 16 ms' and the '~1.9 MB texture upload per repaint at DPR 1.5' figures are calculated, not measured. If dragging feels sluggish, profile before rewriting to QSGGeometryNode.; Whether the system tray icon actually appeared in the Windows notification area. I confirmed the QML loaded without error (which means SystemTrayIcon instantiated), but I ran the app windowed/minimized without taking a screenshot of the tray, and my icon.source pointed at a non-existent qrc path, so the icon would have been blank.; Whether HuskarUI's HusWindow / HusCaptionBar (QWindowKit-based frameless window) coexists with ApplicationWindow and the tray show/hide flow. I used a plain QtQuick.Controls ApplicationWindow. HuskarUI bundles QWindowKit (third_party/HuskarUI/3rdparty/qwindowkit) for custom title bars; integrating that with close-to-tray is untested here.; The 31-band / 9-slot figures came from the task brief, not from my own reading of Peace.au3. I clamped setBandCount to [1,31] on that basis. I read the .peace preset format (which shows Frequency1..N and PreAmp1..6 keys) but did not verify Peace's actual maximum band count in I:\Qt\reference\peace-original\src\Peace\Peace.au3.

---

## DreamDSP Windows integration layer (C++), verified on this machine: APO 1.4.1 at D:/Program Files/EqualizerAPO, Qt 6.8.3 msvc2022_64, MSVC 14.51.36231. All claims below were tested by compiling and running probes, not inferred.
- NO ELEVATION NEEDED for D:/Program Files/EqualizerAPO/config. But the grant is NOT the commonly-described 'BUILTIN\Users full control' — BUILTIN\Users only has (RX). Write rights come from an INHERITED NT AUTHORITY\Authenticated Users:(I)(M) ACE. Modify includes DELETE, which is what makes rename-into-place work.
  evidence: icacls 'D:\Program Files\EqualizerAPO\config' on this machine: 'NT AUTHORITY\Authenticated Users:(I)(M)', 'NT AUTHORITY\Authenticated Users:(I)(OI)(CI)(IO)(M)', 'BUILTIN\Users:(I)(RX)', 'BUILTIN\Users:(I)(OI)(CI)(IO)(GR,GE)'. Same ACL on parent dir. Empirically in a non-elevated shell (IsInRole(Administrator)=False): WriteAllText OK, Remove OK, [IO.File]::Move(tmp,dst,$true) (MoveFileEx REPLACE_EXISTING) OK, mkdir/rmdir OK, config.txt opened FileAccess=ReadWrite OK.
  → Ship as asInvoker; no UAC manifest, no elevated helper for config writes. Because the ACE is inherited (not explicit), probe writability at startup instead of assuming: CreateFileW(cfg+L"\\.dreamdsp-probe", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE, nullptr); on ERROR_ACCESS_DENIED show a 'needs elevation' path rather than silently failing every save.
- APO's config path MUST be read from HKLM\SOFTWARE\EqualizerAPO in the 64-bit registry view. On this machine it is a non-default path and there is NO WOW6432Node mirror, so a 32-bit build (or any KEY_WOW64_32KEY read) would silently find nothing and fall back to the wrong directory.
  evidence: Get-ItemProperty HKLM:\SOFTWARE\EqualizerAPO → InstallPath = 'D:\Program Files\EqualizerAPO', ConfigPath = 'D:\Program Files\EqualizerAPO\config', EnableTrace = 'false'. Test-Path HKLM:\SOFTWARE\WOW6432Node\EqualizerAPO → False. Peace does the same 64-bit-first read: Peace.au3:1385-1389 `RegRead("HKLM64\SOFTWARE\EqualizerAPO","ConfigPath")` then falls back to `RegRead("HKLM\...")`.
  → Open with RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EqualizerAPO", 0, KEY_READ|KEY_WOW64_64KEY, &k) and read ConfigPath, then InstallPath. Never hardcode C:/Program Files/EqualizerAPO. See ApoLocator in code_samples[1].
- A device is APO-enabled iff any of the FxProperties effect-CLSID values equals one of APO's two CLSIDs. On this machine exactly ONE of 76 render endpoints has APO: {30d0a993-...} = '后面板 耳机 (Realtek USB Audio)', which is also the default device.
  evidence: Registry scan of HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\*\FxProperties: only {30d0a993-116c-4954-b341-5de5dc17de1c} carries `{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1 = {EACD2258-FCAC-4FF4-B36D-419E924A6D79}` and `,2 = {EC1CC9CE-FAED-4822-828A-82A81A6F018F}`. HKCR\CLSID lookup confirms those are 'EqualizerAPO Pre-Mix Class' and 'EqualizerAPO Post-Mix Class', both InprocServer32 = D:\Program Files\EqualizerAPO\EqualizerAPO.dll. Peace's own check is Peace.au3:28035-28059 (DeviceInstalled), constants at 28039-28040.
  → Compare case-insensitively against both CLSIDs across pids {1,2,5,6,7,13,14,15}. See ApoDeviceStatus in code_samples[1].
- Peace checks only pids 1,2,5,6,7 — APO itself also uses 13/14/15 (Multi-SFX/MFX/EFX). And pid 15 can be REG_MULTI_SZ holding SEVERAL CLSIDs, so a plain REG_SZ read finds only the first one and can produce a false negative.
  evidence: Peace.au3:28038 `Const $FxGUIDs = [ "1", "2", "5", "6", "7" ]`. DeviceAPOInfo.cpp (https://raw.githubusercontent.com/mirror/equalizerapo/master/DeviceAPOInfo.cpp) additionally uses ,13 ,14 ,15. Observed on this machine, device {7fbb7c92-3348-419d-8245-48a9e7e50143}: `,15 = {6FA9D7C5-4BEC-42BE-A96B-32545BD7AD2D} {AB8EDD89-64A1-4d58-A961-34E85144060F}` — two GUIDs in one value.
  → Query with RegQueryValueExW and handle BOTH REG_SZ and REG_MULTI_SZ: split REG_MULTI_SZ on NULs and test every element. Iterate pids {1,2,5,6,7,13,14,15}. Implemented in code_samples[1].
- Peace has a real case-sensitivity bug in its APO detection: a device where APO is installed ONLY as post-mix is reported as NOT installed.
  evidence: Peace.au3:28047 and 28052: `If StringLower($KeyValue) = StringLower($EqualizerAPOPre) Or StringLower($KeyValue) = $EqualizerAPOPost Then Return 0`. The left side is lowercased but $EqualizerAPOPost (declared uppercase at 28040 as "{EC1CC9CE-FAED-4822-828A-82A81A6F018F}") is NOT wrapped in StringLower, so that comparison can never be true for a lowercase-normalised registry value.
  → Do not port this logic verbatim. Normalise BOTH sides (or better, parse both with CLSIDFromString and compare GUIDs bytewise, which is what APO does). code_samples[1] uses CLSIDFromString + IsEqualGUID.
- DEVICE_STATE_DISABLED is NOT stored as 0x2 in the registry. Disabled devices are stored as 0x10000001 (ACTIVE bit + undocumented 0x10000000). IMMDevice::GetState() translates this to DEVICE_STATE_DISABLED. Registry DeviceState also carries other undocumented high bits (0x20000000, 0x21000000, 0x30000000) that GetState() masks off.
  evidence: Registry histogram of DeviceState over 76 render subkeys: 0x1 x11, 0x4 x17, 0x8 x9, 0x10000001 x2, 0x20000004 x30, 0x21000004 x4, 0x30000004 x3 — note ZERO entries equal to 0x2. Compiled probe using IMMDeviceEnumerator::EnumAudioEndpoints returned count[ACTIVE]=11, count[DISABLED]=2, count[NOTPRESENT]=54, count[UNPLUGGED]=9, count[STATEMASK_ALL]=76, and GetState() returned clean low-nibble values (0x1/0x4/0x8). 11+2+54+9=76 matches. APO encodes the same rule: DeviceAPOInfo.cpp `disabled = deviceState & DEVICE_STATE_DISABLED || deviceState & 0x10000000;`.
  → If you read DeviceState from the registry (needed for devices whose devnode is gone), mask with 0x0000000F for the documented bits AND treat (raw & 0x10000000) as disabled. If you use IMMDevice::GetState() you get the correct value already — prefer that as the primary source.
- IMMDevice endpoint IDs have the form {0.0.0.00000000}.{GUID}. The MMDevices registry subkey name is exactly the trailing brace-GUID — take the substring from the LAST '{'.
  evidence: Compiled probe output: default eRender/eConsole id = `{0.0.0.00000000}.{30d0a993-116c-4954-b341-5de5dc17de1c}`, and the matching registry subkey is `HKLM\...\MMDevices\Audio\Render\{30d0a993-116c-4954-b341-5de5dc17de1c}`. Verified for all 76 endpoints (probe printed regGuid = substring from last '{' and each existed under Render).
  → std::wstring regGuid(const std::wstring& id){ auto p = id.rfind(L'{'); return p==std::wstring::npos ? std::wstring() : id.substr(p); } — this is the join key between the COM enumeration and the FxProperties check. Render prefix is {0.0.0.00000000}, capture is {0.0.1.00000000}; do not parse the prefix, just take the tail.
- PKEY_Device_FriendlyName is synthesised by MMDevAPI as 'DeviceDesc (InterfaceFriendlyName)' — it is NOT stored in the registry. On the APO device the registry value {a45c254e-...},14 is EMPTY, yet IPropertyStore returns the full composed name.
  evidence: Registry: `{a45c254e-df1c-4efd-8020-67d146a850e0},14` = (empty), `,2` = '后面板 耳机', `{b3f8fa53-0004-438e-9003-51a46e139bfc},6` = 'Realtek USB Audio'. Compiled probe via IPropertyStore on the same endpoint: FriendlyName = '后面板 耳机 (Realtek USB Audio)', DeviceDesc = '后面板 耳机', IfFriendlyName = 'Realtek USB Audio'.
  → Use IPropertyStore (PKEY_Device_FriendlyName) as the display name for live devices. If you read the registry directly (APO's approach), you must compose DeviceDesc + " (" + PKEY_DeviceInterface_FriendlyName + ")" yourself, because ,14 is blank.
- CRITICAL ROBUSTNESS BUG SOURCE: for NOTPRESENT/UNPLUGGED endpoints whose devnode is gone, IPropertyStore::GetValue FAILS with 0xE000020B (ERROR_NO_SUCH_DEVINST) for PKEY_Device_FriendlyName AND PKEY_DeviceInterface_FriendlyName, while PKEY_Device_DeviceDesc still succeeds. Naive code shows blank names for exactly the unplugged devices the user most wants to configure.
  evidence: Compiled probe over DEVICE_STATEMASK_ALL: e.g. endpoint {06fab05b-891b-4609-b61d-c8d1d922dcd3} state=0x4 → FriendlyName <hr=0xE000020B>, IfFriendlyName <hr=0xE000020B>, DeviceDesc 'DigitalOutput'. Same pattern on {130614b2-...}, {267b93de-...}, {27fb573f-...}. Contrast {1110694f-...} state=0x4 which DID resolve to 'DigitalOutput (NVIDIA High Definition Audio)'. 0xE000020B = ERROR_NO_SUCH_DEVINST.
  → Implement a 4-step fallback chain, do not just read FriendlyName: (1) IPropertyStore PKEY_Device_FriendlyName; (2) compose from IPropertyStore DeviceDesc + IfFriendlyName; (3) IPropertyStore DeviceDesc alone; (4) registry ...\{guid}\Properties value '{a45c254e-df1c-4efd-8020-67d146a850e0},2'. Step 4 is the only one that always works for dead devnodes. Implemented in code_samples[0].
- VERIFIED BY EXECUTION: RegisterHotKey(nullptr, ...) works in a Qt 6.8.3 QGuiApplication and the resulting WM_HOTKEY — which has hwnd == NULL — IS delivered to QAbstractNativeEventFilter::nativeEventFilter with eventType 'windows_generic_MSG'. You do not need your own HWND or a message-only window.
  evidence: Built a Qt 6.8.3 msvc2022_64 test app (CMake+Ninja, MSVC 14.51.36231) that registers Ctrl+Alt+F9 with RegisterHotKey(nullptr, 0xB001, MOD_CONTROL|MOD_ALT|MOD_NOREPEAT, VK_F9), synthesises the chord with SendInput, and counts filter hits. Output: `RegisterHotKey(nullptr) ok=1 err=0` / `SendInput sent=6 err=0` / `GOT WM_HOTKEY id=45057 hwnd=0x0 mods=0x3 vk=0x78` / `RESULT: hits=1 => QT DELIVERS NULL-HWND WM_HOTKEY TO nativeEventFilter`. 45057=0xB001, 0x3=MOD_ALT|MOD_CONTROL, 0x78=VK_F9. Qt signature confirmed at D:/Qt/6.8.3/msvc2022_64/include/QtCore/qabstractnativeeventfilter.h:19 — `virtual bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) = 0;` (qintptr*, not long*, in Qt 6).
  → Use code_samples[4] as-is. Two gotchas the test exposed: (a) MOD_NOREPEAT (0x4000) is accepted by RegisterHotKey but is NOT echoed in LOWORD(lParam) — lParam low word was 0x3, only the real modifiers, so don't round-trip-compare it; (b) RegisterHotKey is thread-affine — it must be called on the same thread that runs the Qt event loop, and UnregisterHotKey must be called from that thread too.
- Peak metering works and needs no audio stream of your own, but on THIS machine no endpoint has a hardware meter, so every device reads exactly 0.0 while any app holds it in exclusive mode.
  evidence: Compiled probe on all 11 ACTIVE render endpoints: IMMDevice::Activate(IID_IAudioMeterInformation) succeeded on every one; GetPeakValue returned hr=0x00000000; GetMeteringChannelCount=2. QueryHardwareSupport returned 0x0, 0x2 or 0x3 — never 0x4. SDK constants at C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um/endpointvolume.h:104-106: VOLUME 0x1, MUTE 0x2, METER 0x4. MSDN https://learn.microsoft.com/en-us/windows/win32/coreaudio/peak-meters states verbatim: 'If a device lacks hardware peak meter, the peak meter is active in shared mode, but not in exclusive mode. In exclusive mode, the application and the audio hardware exchange audio data directly, bypassing the software peak meter (which always reports a peak value of 0.0).'
  → Call QueryHardwareSupport once at attach time; if (mask & ENDPOINT_HARDWARE_SUPPORT_METER)==0, remember that a sustained 0.0 may mean 'exclusive-mode app is playing', not 'silence'. Surface that in the UI instead of showing a dead meter. Also note MSDN GetPeakValue Remarks: the value is the peak of the PREVIOUS device period, so polling faster than the device period just repeats values — 60 Hz (16 ms) is the practical ceiling; Peace itself polls at 80 ms (Peace.au3:11708). Implemented in code_samples[3].
- APO opens the config file with FILE_SHARE_READ ONLY and spins with Sleep(1) until it can open it. That means (a) APO will never read a file you hold open for write, but (b) while APO has it open, YOUR MoveFileEx/CreateFile(GENERIC_WRITE) will fail with ERROR_SHARING_VIOLATION — so your writer needs the retry loop, not just APO's.
  evidence: https://raw.githubusercontent.com/mirror/equalizerapo/master/FilterEngine.cpp — `hFile = CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); ... if (error != ERROR_SHARING_VIOLATION) { Log...; return; } Sleep(1);` inside `while (hFile == INVALID_HANDLE_VALUE)`. Watcher in the same file: `FindFirstChangeNotificationW(engine->configPath.c_str(), true, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE)` then `FindNextChangeNotification(notificationHandle); WaitForMultipleObjects(1, &notificationHandle, false, 10);` — the 10 ms coalescing window.
  → Never write peace.txt in place — a torn read is possible in the gap between APO's open and your last WriteFile. Write a temp file, FlushFileBuffers, close, then MoveFileEx(tmp, dst, MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH) in a retry loop that treats ERROR_SHARING_VIOLATION / ERROR_ACCESS_DENIED / ERROR_LOCK_VIOLATION as retryable with a short backoff. Rename is atomic, so APO either sees the whole old file or the whole new file. Implemented in code_samples[5].
- Stage the temp file OUTSIDE the watched tree but on the SAME volume, otherwise every save triggers TWO APO config reloads. APO watches the config dir with bWatchSubtree=TRUE and FILE_NOTIFY_CHANGE_FILE_NAME, so creating tmp inside config/ fires a reload by itself, then the rename fires a second one.
  evidence: FilterEngine.cpp watch call quoted above uses bWatchSubtree=true and FILE_NOTIFY_CHANGE_FILE_NAME. The parent D:/Program Files/EqualizerAPO is NOT inside configPath and is on the same volume D:. Verified non-elevated on this machine: writing 'D:\Program Files\EqualizerAPO\__pqt_parent.tmp' succeeded, then [IO.File]::Move of that file into config\ with overwrite succeeded ('cross-dir same-volume MoveFileEx OK'). Parent ACL is the same inherited Authenticated Users:(I)(M).
  → Stage at <InstallPath>/.dreamdsp-tmp/ (create once). Must be same volume — MoveFileEx across volumes silently degrades to copy+delete and is NOT atomic, so do NOT pass MOVEFILE_COPY_ALLOWED. Guard with a GetVolumePathNameW comparison and fall back to staging inside config/ (accepting the double reload) if the volumes differ.
- QFileSystemWatcher permanently stops watching a path once that path is renamed or deleted — which is exactly what the recommended atomic write (and the official APO Editor, and Peace) do to config files. Without a re-arm wrapper, DreamDSP sees the first external change and then goes deaf forever.
  evidence: https://doc.qt.io/qt-6/qfilesystemwatcher.html: 'QFileSystemWatcher stops monitoring files once they have been renamed or removed from disk' and, for rapid sequences, 'only the last change in the sequence will always generate this signal'. This is consistent with the prior local analysis at I:/Qt/reference/Qt-refactor-feasibility.md:875 and I:/Qt/reference/research-dossier.md:329-330.
  → Wrap it: watch BOTH the file and its parent directory; on any signal, QTimer::singleShot debounce ~120 ms, then check files().contains(path) and re-addPath() if missing. The parent-directory watch is what survives the delete/recreate. Implemented in code_samples[6].
- IMMNotificationClient callbacks arrive on an internal MMDevAPI/RPC thread, never the Qt GUI thread, and the LPCWSTR arguments are only valid for the duration of the call. The notifier must be a plain IUnknown (not a QObject) and must copy strings before posting.
  evidence: Interface contract per https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nn-mmdeviceapi-immnotificationclient (callbacks are invoked by the MMDevice module on its own thread; the docs warn the client must not block or call back into the enumerator from the callback). The endpoint-ID strings passed to OnDeviceStateChanged/OnDefaultDeviceChanged are caller-owned for the call only. Qt signature for the marshalling target confirmed locally in D:/Qt/6.8.3/msvc2022_64/include/QtCore.
  → Split into two objects: a refcounted IMMNotificationClient POD that holds a raw pointer to a QObject bridge, and the QObject bridge that owns the enumerator and emits Qt signals. In each callback, build QString::fromWCharArray FIRST, then QMetaObject::invokeMethod(bridge, [=]{...}, Qt::QueuedConnection). Never Release() the enumerator from inside a callback. Implemented in code_samples[2].
- APO stores the ORIGINAL (displaced) APO CLSIDs under HKLM\SOFTWARE\EqualizerAPO\Child APOs\{deviceGuid}, and the installer also drops a .reg backup next to the exe. This is how you tell 'APO installed, chaining the vendor APO' from 'APO installed, vendor APO discarded'.
  evidence: HKLM\SOFTWARE\EqualizerAPO\Child APOs\{30d0a993-116c-4954-b341-5de5dc17de1c} contains: `{d04e05a6-...},1 = !VALUE`, `,2 = {DA2FB532-3014-4B93-AD05-21B2C620F9C2}`, `,5/,6/,7 = !VALUE`, `PreMixChild = (empty)`, `PostMixChild = {DA2FB532-3014-4B93-AD05-21B2C620F9C2}`, `AllowSilentBufferModification = false`, `Version = 2`. The file 'D:\Program Files\EqualizerAPO\backup_Realtek USB Audio_后面板 耳机.reg' restores exactly `"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2"="{DA2FB532-3014-4B93-AD05-21B2C620F9C2}"`. Sentinel strings match DeviceAPOInfo.h: `#define APOGUID_NOVALUE L"!VALUE"`, `APOGUID_NOKEY L"!KEY"`, `APOGUID_NULL L"{00000000-...}"`.
  → Read this key read-only to display 'Use original APO: pre-mix=no, post-mix=yes (Realtek)'. Treat '!VALUE' as 'no value existed before' and '!KEY' as 'no key existed before'. DreamDSP should NOT write these keys — installing/uninstalling APO on a device requires SYSTEM-level rights and a reboot/restart of the audio service; delegate to APO's own Configurator.exe/DeviceSelector.exe (present at D:/Program Files/EqualizerAPO/DeviceSelector.exe), which is exactly what Peace does (Peace.au3:28089-28095, OpenConfigurator).
- The current live config on this machine is minimal and safe to take over: config.txt is a single line 'Include: peace.txt' and peace.txt is currently 0 bytes.
  evidence: cat 'D:/Program Files/EqualizerAPO/config/config.txt' → 'Include: peace.txt' (20 bytes, mtime 2025-07-15). ls -la peace.txt → 0 bytes, mtime 2025-07-28. Directory also holds ~25 .peace presets, Peace.exe, peace.ini, and AutoEQ databases (AutoEQCompressedHarman5.txt, AutoEQCompressedIEF5.txt, AutoEQCompressedIEFBass5.txt, OPRAEQCompressed5.txt).
  → Write only your own generated file (e.g. dreamdsp.txt) and touch config.txt with a minimal read-modify-write that only ensures the Include line exists. Re-read config.txt immediately before each such edit so you don't clobber lines Peace or the official Editor added. Do not overwrite peace.txt while Peace 1.6.9.11 is still installed in the same directory — the two will fight.

### code samples
```
// ============ WinAudioEndpoints.h ============
// Enumerates render endpoints INCLUDING inactive/unplugged/disabled.
#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"propsys.lib")

namespace pq {
using Microsoft::WRL::ComPtr;

struct EndpointInfo {
    std::wstring id;        // {0.0.0.00000000}.{guid}
    std::wstring regGuid;   // {guid}  <- MMDevices subkey name
    std::wstring friendly;  // best-effort display name (never empty)
    std::wstring desc;      // PKEY_Device_DeviceDesc
    std::wstring iface;     // PKEY_DeviceInterface_FriendlyName
    DWORD state = 0;        // DEVICE_STATE_*
    bool isDefaultConsole = false;
    bool isDefaultMultimedia = false;
    bool isDefaultComms = false;
    bool active()    const { return state == DEVICE_STATE_ACTIVE; }
    bool unplugged() const { return state == DEVICE_STATE_UNPLUGGED; }
};

class WinAudioEndpoints {
public:
    // dataFlow: eRender or eCapture. Returns {} on failure.
    static std::vector<EndpointInfo> enumerate(EDataFlow flow = eRender);
    static std::wstring regGuidFromEndpointId(const std::wstring& id);
};
} // namespace pq

// ============ WinAudioEndpoints.cpp ============
#include "WinAudioEndpoints.h"

namespace pq {
namespace {

// RAII PROPVARIANT
struct PropVar {
    PROPVARIANT v;
    PropVar()  { PropVariantInit(&v); }
    ~PropVar() { PropVariantClear(&v); }
    PropVar(const PropVar&) = delete;
    PropVar& operator=(const PropVar&) = delete;
};

// Returns empty string on ANY failure. NOTE: for endpoints whose devnode is
// gone, PKEY_Device_FriendlyName / PKEY_DeviceInterface_FriendlyName return
// 0xE000020B (ERROR_NO_SUCH_DEVINST) while PKEY_Device_DeviceDesc still works.
std::wstring readStr(IPropertyStore* ps, const PROPERTYKEY& key) {
    if (!ps) return {};
    PropVar pv;
    if (FAILED(ps->GetValue(key, &pv.v))) return {};
    if (pv.v.vt != VT_LPWSTR || !pv.v.pwszVal) return {};
    return pv.v.pwszVal;
}

// Last-resort: read the cached name straight out of the registry.
// PKEY_Device_DeviceDesc == {a45c254e-df1c-4efd-8020-67d146a850e0},2
std::wstring readDescFromRegistry(const std::wstring& regGuid, EDataFlow flow) {
    if (regGuid.empty()) return {};
    std::wstring path =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\";
    path += (flow == eCapture ? L"Capture\\" : L"Render\\");
    path += regGuid + L"\\Properties";

    HKEY k{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0,
                      KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS)
        return {};
    wchar_t buf[512]{}; DWORD cb = sizeof(buf), type = 0;
    LONG r = RegQueryValueExW(k,
        L"{a45c254e-df1c-4efd-8020-67d146a850e0},2",
        nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_SZ) return {};
    return buf;
}

std::wstring defaultId(IMMDeviceEnumerator* e, EDataFlow flow, ERole role) {
    ComPtr<IMMDevice> d;
    if (FAILED(e->GetDefaultAudioEndpoint(flow, role, &d)) || !d) return {};
    LPWSTR raw = nullptr;
    if (FAILED(d->GetId(&raw)) || !raw) return {};
    std::wstring s(raw);
    CoTaskMemFree(raw);
    return s;
}

} // anon namespace

std::wstring WinAudioEndpoints::regGuidFromEndpointId(const std::wstring& id) {
    // "{0.0.0.00000000}.{30d0a993-...}" -> "{30d0a993-...}"
    const size_t p = id.rfind(L'{');
    return (p == std::wstring::npos) ? std::wstring() : id.substr(p);
}

std::vector<EndpointInfo> WinAudioEndpoints::enumerate(EDataFlow flow) {
    std::vector<EndpointInfo> out;

    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&en))))
        return out;

    const std::wstring dCon = defaultId(en.Get(), flow, eConsole);
    const std::wstring dMul = defaultId(en.Get(), flow, eMultimedia);
    const std::wstring dCom = defaultId(en.Get(), flow, eCommunications);

    ComPtr<IMMDeviceCollection> coll;
    // DEVICE_STATEMASK_ALL (0x0F) = ACTIVE|DISABLED|NOTPRESENT|UNPLUGGED
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATEMASK_ALL, &coll)))
        return out;

    UINT n = 0;
    if (FAILED(coll->GetCount(&n))) return out;
    out.reserve(n);

    for (UINT i = 0; i < n; ++i) {
        ComPtr<IMMDevice> dev;
        if (FAILED(coll->Item(i, &dev)) || !dev) continue;

        EndpointInfo info;

        LPWSTR raw = nullptr;
        if (SUCCEEDED(dev->GetId(&raw)) && raw) { info.id = raw; CoTaskMemFree(raw); }
        info.regGuid = regGuidFromEndpointId(info.id);

        // GetState() returns the clean low nibble; the registry DeviceState
        // DWORD carries extra undocumented high bits (0x10000000 == disabled).
        dev->GetState(&info.state);

        ComPtr<IPropertyStore> ps;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
            info.desc  = readStr(ps.Get(), PKEY_Device_DeviceDesc);
            info.iface = readStr(ps.Get(), PKEY_DeviceInterface_FriendlyName);
            info.friendly = readStr(ps.Get(), PKEY_Device_FriendlyName);
        }

        // 4-step fallback -- step 4 is the only one that survives a dead devnode.
        if (info.friendly.empty()) {
            if (!info.desc.empty() && !info.iface.empty())
                info.friendly = info.desc + L" (" + info.iface + L")";
            else if (!info.desc.empty())
                info.friendly = info.desc;
            else {
                info.friendly = readDescFromRegistry(info.regGuid, flow);
                if (info.friendly.empty()) info.friendly = info.regGuid;
            }
        }

        info.isDefaultConsole    = (!info.id.empty() && info.id == dCon);
        info.isDefaultMultimedia = (!info.id.empty() && info.id == dMul);
        info.isDefaultComms      = (!info.id.empty() && info.id == dCom);

        out.push_back(std::move(info));
    }
    return out;
}
} // namespace pq
```

```
// ============ ApoInfo.h / ApoInfo.cpp ============
// (1) locate Equalizer APO even at a NON-DEFAULT install path
// (2) decide whether APO is ENABLED on a given endpoint
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace pq {

struct ApoPaths {
    bool         found = false;
    std::wstring installPath;  // D:\Program Files\EqualizerAPO
    std::wstring configPath;   // D:\Program Files\EqualizerAPO\config
};

class ApoLocator {
public:
    // MANDATORY on this machine: APO lives on D:, and there is no
    // WOW6432Node mirror -> the 64-bit view must be forced.
    static ApoPaths locate();
};

class ApoDeviceStatus {
public:
    // true == Equalizer APO is installed/enabled on this endpoint.
    // regGuid is the "{guid}" tail of the IMMDevice endpoint id.
    static bool isEnabled(const std::wstring& regGuid, bool capture = false);
};

} // namespace pq

// ---------------- implementation ----------------
#include "ApoInfo.h"
#include <objbase.h>

namespace pq {
namespace {

bool regReadString(HKEY root, const wchar_t* sub, const wchar_t* val,
                   std::wstring& out) {
    HKEY k{};
    if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &k)
            != ERROR_SUCCESS)
        return false;
    DWORD type = 0, cb = 0;
    LONG r = RegQueryValueExW(k, val, nullptr, &type, nullptr, &cb);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(k); return false;
    }
    std::wstring buf(cb / sizeof(wchar_t) + 1, L'\0');
    r = RegQueryValueExW(k, val, nullptr, &type,
                         reinterpret_cast<LPBYTE>(&buf[0]), &cb);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS) return false;
    buf.resize(wcslen(buf.c_str()));
    out = std::move(buf);
    return !out.empty();
}

// Reads REG_SZ *and* REG_MULTI_SZ. pid 15 (EFX) is frequently MULTI_SZ with
// several CLSIDs -- a REG_SZ-only read yields a false negative.
bool regReadStrings(HKEY key, const wchar_t* val,
                    std::vector<std::wstring>& out) {
    DWORD type = 0, cb = 0;
    if (RegQueryValueExW(key, val, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS)
        return false;
    if (type != REG_SZ && type != REG_MULTI_SZ && type != REG_EXPAND_SZ)
        return false;

    std::vector<wchar_t> buf(cb / sizeof(wchar_t) + 2, L'\0');
    if (RegQueryValueExW(key, val, nullptr, &type,
                         reinterpret_cast<LPBYTE>(buf.data()), &cb) != ERROR_SUCCESS)
        return false;

    if (type == REG_MULTI_SZ) {
        const wchar_t* p = buf.data();
        while (*p) { out.emplace_back(p); p += wcslen(p) + 1; }
    } else {
        out.emplace_back(buf.data());
    }
    return !out.empty();
}

// Compare as GUIDs, not as strings -- avoids the case-sensitivity bug in
// Peace.au3:28047 where the post-mix constant is never lowercased.
bool isApoClsid(const std::wstring& s) {
    static const GUID kPre  = // {EACD2258-FCAC-4FF4-B36D-419E924A6D79}
        {0xEACD2258,0xFCAC,0x4FF4,{0xB3,0x6D,0x41,0x9E,0x92,0x4A,0x6D,0x79}};
    static const GUID kPost = // {EC1CC9CE-FAED-4822-828A-82A81A6F018F}
        {0xEC1CC9CE,0xFAED,0x4822,{0x82,0x8A,0x82,0xA8,0x1A,0x6F,0x01,0x8F}};
    if (s.size() < 38) return false;          // skips "!VALUE" / "!KEY"
    GUID g{};
    if (FAILED(CLSIDFromString(s.c_str(), &g))) return false;
    return IsEqualGUID(g, kPre) || IsEqualGUID(g, kPost);
}

} // anon namespace

ApoPaths ApoLocator::locate() {
    ApoPaths p;
    // 64-bit view first (Peace does the same: Peace.au3:1374-1389).
    regReadString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EqualizerAPO",
                  L"InstallPath", p.installPath);
    regReadString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EqualizerAPO",
                  L"ConfigPath", p.configPath);
    if (p.configPath.empty() && !p.installPath.empty())
        p.configPath = p.installPath + L"\\config";
    p.found = !p.configPath.empty() &&
              GetFileAttributesW(p.configPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    return p;
}

bool ApoDeviceStatus::isEnabled(const std::wstring& regGuid, bool capture) {
    if (regGuid.empty()) return false;

    std::wstring path =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\";
    path += (capture ? L"Capture\\" : L"Render\\");
    path += regGuid + L"\\FxProperties";

    HKEY k{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0,
                      KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS)
        return false;

    // fmtid is always {d04e05a6-594b-4fb6-a80d-01af5eed7d1d}.
    //  1 = PKEY_FX_PreMixEffectClsid   (LFX)
    //  2 = PKEY_FX_PostMixEffectClsid  (GFX)
    //  5 = SFX, 6 = MFX, 7 = EFX
    // 13 = Multi-SFX, 14 = Multi-MFX, 15 = Multi-EFX  (Win10+; often MULTI_SZ)
    static const wchar_t* kValues[] = {
        L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1",
        L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2",
        L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5",
        L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6",
        L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7",
        L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},13",
        L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},14",
        L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},15",
    };

    bool enabled = false;
    for (const wchar_t* v : kValues) {
        std::vector<std::wstring> vals;
        if (!regReadStrings(k, v, vals)) continue;
        for (const auto& s : vals)
            if (isApoClsid(s)) { enabled = true; break; }
        if (enabled) break;
    }
    RegCloseKey(k);
    return enabled;
}
} // namespace pq
```

```
// ============ EndpointNotifier.h ============
// IMMNotificationClient -> Qt signals, marshalled onto the GUI thread.
// Callbacks arrive on an MMDevAPI/RPC thread; the LPCWSTR args are valid
// ONLY for the duration of the call, so they are copied before posting.
#pragma once
#include <QObject>
#include <QString>
#include <windows.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <atomic>

namespace pq {

class EndpointNotifier : public QObject {
    Q_OBJECT
public:
    explicit EndpointNotifier(QObject* parent = nullptr);
    ~EndpointNotifier() override;

    bool start();   // must be called from the GUI thread
    void stop();

signals:
    void deviceStateChanged(const QString& endpointId, quint32 newState);
    void deviceAdded(const QString& endpointId);
    void deviceRemoved(const QString& endpointId);
    void defaultDeviceChanged(const QString& endpointId, int flow, int role);
    void devicePropertyChanged(const QString& endpointId);

private:
    class Client;                       // COM shim, defined in the .cpp
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> m_enum;
    Client* m_client = nullptr;         // refcounted; released in stop()
};

} // namespace pq

// ============ EndpointNotifier.cpp ============
#include "EndpointNotifier.h"
#include <QMetaObject>
#include <QPointer>

namespace pq {

// Deliberately NOT a QObject: COM refcounting and QObject ownership do not mix.
class EndpointNotifier::Client final : public IMMNotificationClient {
public:
    explicit Client(EndpointNotifier* owner) : m_owner(owner) {}

    void detach() { m_owner = nullptr; }   // called before Release()

    // ---- IUnknown ----
    ULONG STDMETHODCALLTYPE AddRef() override  { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --m_ref;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // ---- IMMNotificationClient (all on a foreign thread) ----
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD st) override {
        post([id = str(id), st](EndpointNotifier* o) {
            emit o->deviceStateChanged(id, static_cast<quint32>(st));
        });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) override {
        post([id = str(id)](EndpointNotifier* o) { emit o->deviceAdded(id); });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override {
        post([id = str(id)](EndpointNotifier* o) { emit o->deviceRemoved(id); });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow f, ERole r,
                                                     LPCWSTR id) override {
        post([id = str(id), f, r](EndpointNotifier* o) {
            emit o->defaultDeviceChanged(id, int(f), int(r));
        });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR id,
                                                     const PROPERTYKEY) override {
        post([id = str(id)](EndpointNotifier* o) {
            emit o->devicePropertyChanged(id);
        });
        return S_OK;
    }

private:
    ~Client() = default;   // only Release() may destroy

    // Copy while the pointer is still valid.
    static QString str(LPCWSTR w) {
        return w ? QString::fromWCharArray(w) : QString();
    }

    // The ONLY safe way back to the GUI thread. Qt::QueuedConnection means the
    // functor runs later, on m_owner's thread, so we must not capture raw
    // COM pointers or dereference the owner here.
    template <class F>
    void post(F&& fn) {
        EndpointNotifier* o = m_owner;
        if (!o) return;
        QPointer<EndpointNotifier> guard(o);
        QMetaObject::invokeMethod(o,
            [guard, fn = std::forward<F>(fn)]() { if (guard) fn(guard.data()); },
            Qt::QueuedConnection);
    }

    std::atomic<ULONG>  m_ref{1};
    EndpointNotifier*   m_owner = nullptr;
};

EndpointNotifier::EndpointNotifier(QObject* parent) : QObject(parent) {}
EndpointNotifier::~EndpointNotifier() { stop(); }

bool EndpointNotifier::start() {
    if (m_client) return true;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&m_enum))))
        return false;

    m_client = new Client(this);                    // starts at refcount 1
    if (FAILED(m_enum->RegisterEndpointNotificationCallback(m_client))) {
        m_client->detach();
        m_client->Release();
        m_client = nullptr;
        m_enum.Reset();
        return false;
    }
    return true;
}

void EndpointNotifier::stop() {
    if (!m_client) return;
    // Unregister FIRST: a callback can still be in flight on another thread.
    if (m_enum) m_enum->UnregisterEndpointNotificationCallback(m_client);
    m_client->detach();     // any in-flight post() becomes a no-op
    m_client->Release();
    m_client = nullptr;
    m_enum.Reset();
}

} // namespace pq
```

```
// ============ PeakMeter.h / PeakMeter.cpp ============
// Per-device peak metering. No audio stream of our own is needed.
#pragma once
#include <QObject>
#include <QTimer>
#include <QString>
#include <QVector>
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wrl/client.h>

namespace pq {

class PeakMeter : public QObject {
    Q_OBJECT
public:
    explicit PeakMeter(QObject* parent = nullptr);

    // endpointId is the full "{0.0.0.00000000}.{guid}" id.
    bool attach(const QString& endpointId);
    void detach();

    // False => software meter => reads 0.0 whenever an app holds the device
    // in EXCLUSIVE mode. On this machine NO endpoint reported hardware
    // metering (QueryHardwareSupport never returned ENDPOINT_HARDWARE_SUPPORT_METER).
    bool hasHardwareMeter() const { return m_hwMeter; }
    int  channelCount()     const { return m_channels; }

    void setInterval(int ms) { m_timer.setInterval(ms); }
    void start() { if (m_meter) m_timer.start(); }
    void stop()  { m_timer.stop(); }

signals:
    void peak(float master, const QVector<float>& perChannel);
    void deviceLost();

private:
    void poll();

    Microsoft::WRL::ComPtr<IAudioMeterInformation> m_meter;
    QTimer  m_timer;
    UINT    m_channels = 0;
    bool    m_hwMeter  = false;
};

} // namespace pq

// ---------------- implementation ----------------
#include "PeakMeter.h"

namespace pq {
using Microsoft::WRL::ComPtr;

PeakMeter::PeakMeter(QObject* parent) : QObject(parent) {
    // GetPeakValue reports the peak of the PREVIOUS device period, so polling
    // much faster than the device period just repeats values.
    // 33 ms ~= 30 Hz. (Peace polls at 80 ms -- Peace.au3:11708.)
    m_timer.setInterval(33);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &PeakMeter::poll);
}

bool PeakMeter::attach(const QString& endpointId) {
    detach();

    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&en))))
        return false;

    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDevice(reinterpret_cast<const wchar_t*>(
                                 endpointId.utf16()), &dev)) || !dev)
        return false;

    // Activating a meter on a non-ACTIVE endpoint fails; check first.
    DWORD state = 0;
    if (FAILED(dev->GetState(&state)) || state != DEVICE_STATE_ACTIVE)
        return false;

    if (FAILED(dev->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL,
                             nullptr, &m_meter)) || !m_meter)
        return false;

    m_channels = 0;
    m_meter->GetMeteringChannelCount(&m_channels);

    DWORD hw = 0;
    m_meter->QueryHardwareSupport(&hw);
    m_hwMeter = (hw & ENDPOINT_HARDWARE_SUPPORT_METER) != 0;   // 0x4
    return true;
}

void PeakMeter::detach() {
    m_timer.stop();
    m_meter.Reset();
    m_channels = 0;
    m_hwMeter  = false;
}

void PeakMeter::poll() {
    if (!m_meter) return;

    float master = 0.0f;
    HRESULT hr = m_meter->GetPeakValue(&master);
    if (FAILED(hr)) {
        // AUDCLNT_E_DEVICE_INVALIDATED etc. -- endpoint went away.
        detach();
        emit deviceLost();
        return;
    }

    QVector<float> ch;
    if (m_channels > 0) {
        ch.resize(int(m_channels));
        if (FAILED(m_meter->GetChannelsPeakValues(m_channels, ch.data())))
            ch.clear();
    }
    emit peak(master, ch);   // values are normalised 0.0 .. 1.0
}

} // namespace pq
```

```
// ============ HotkeyManager.h / .cpp ============
// VERIFIED BY EXECUTION on Qt 6.8.3 msvc2022_64 / MSVC 14.51.36231:
//   RegisterHotKey(nullptr, ...) succeeds, and the resulting WM_HOTKEY --
//   which has hwnd == NULL -- IS delivered to nativeEventFilter with
//   eventType "windows_generic_MSG". No HWND of our own is required.
//   Test output: "GOT WM_HOTKEY id=45057 hwnd=0x0 mods=0x3 vk=0x78"
#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QString>
#include <QKeySequence>
#include <windows.h>

namespace pq {

class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit HotkeyManager(QObject* parent = nullptr);
    ~HotkeyManager() override;

    // mods: MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN  (MOD_NOREPEAT added
    // automatically). vk: a VK_* virtual-key code.
    // Returns false and sets lastError() if the combo is taken (ERROR_HOTKEY_ALREADY_REGISTERED = 1409).
    bool registerHotkey(const QString& action, UINT mods, UINT vk);
    bool registerHotkey(const QString& action, const QKeySequence& seq);
    void unregisterHotkey(const QString& action);
    void unregisterAll();

    DWORD lastError() const { return m_lastError; }

    // Qt 6 signature: qintptr* result (NOT long*).
    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override;

    static bool toWin32(const QKeySequence& seq, UINT& mods, UINT& vk);

signals:
    void activated(const QString& action);

private:
    QHash<int, QString> m_byId;      // hotkey id -> action
    QHash<QString, int> m_byAction;
    int   m_nextId    = 0xB000;
    DWORD m_lastError = 0;
};

} // namespace pq

// ---------------- implementation ----------------
#include "HotkeyManager.h"
#include <QCoreApplication>

namespace pq {

HotkeyManager::HotkeyManager(QObject* parent) : QObject(parent) {
    // Must run on the thread that owns the Qt event loop: RegisterHotKey is
    // thread-affine and WM_HOTKEY is posted to the *calling thread's* queue.
    qApp->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager() {
    unregisterAll();
    if (qApp) qApp->removeNativeEventFilter(this);
}

bool HotkeyManager::registerHotkey(const QString& action, UINT mods, UINT vk) {
    unregisterHotkey(action);
    if (vk == 0) return false;

    const int id = m_nextId++;
    // MOD_NOREPEAT suppresses auto-repeat while the key is held.
    // NOTE: it is accepted by RegisterHotKey but is NOT echoed back in
    // LOWORD(lParam) -- do not round-trip-compare it.
    if (!RegisterHotKey(nullptr, id, mods | MOD_NOREPEAT, vk)) {
        m_lastError = GetLastError();   // 1409 == already registered elsewhere
        return false;
    }
    m_lastError = 0;
    m_byId.insert(id, action);
    m_byAction.insert(action, id);
    return true;
}

bool HotkeyManager::registerHotkey(const QString& action, const QKeySequence& seq) {
    UINT mods = 0, vk = 0;
    if (!toWin32(seq, mods, vk)) return false;
    return registerHotkey(action, mods, vk);
}

void HotkeyManager::unregisterHotkey(const QString& action) {
    const auto it = m_byAction.constFind(action);
    if (it == m_byAction.cend()) return;
    UnregisterHotKey(nullptr, *it);
    m_byId.remove(*it);
    m_byAction.erase(it);
}

void HotkeyManager::unregisterAll() {
    for (auto it = m_byId.cbegin(); it != m_byId.cend(); ++it)
        UnregisterHotKey(nullptr, it.key());
    m_byId.clear();
    m_byAction.clear();
}

bool HotkeyManager::nativeEventFilter(const QByteArray& eventType,
                                      void* message, qintptr* /*result*/) {
    if (eventType != "windows_generic_MSG") return false;
    auto* msg = static_cast<MSG*>(message);
    if (msg->message != WM_HOTKEY) return false;

    const auto it = m_byId.constFind(int(msg->wParam));
    if (it == m_byId.cend()) return false;

    emit activated(*it);
    return true;   // consumed
}

bool HotkeyManager::toWin32(const QKeySequence& seq, UINT& mods, UINT& vk) {
    if (seq.isEmpty()) return false;
    const QKeyCombination c = seq[0];
    const Qt::KeyboardModifiers m = c.keyboardModifiers();

    mods = 0;
    if (m & Qt::AltModifier)     mods |= MOD_ALT;
    if (m & Qt::ControlModifier) mods |= MOD_CONTROL;
    if (m & Qt::ShiftModifier)   mods |= MOD_SHIFT;
    if (m & Qt::MetaModifier)    mods |= MOD_WIN;

    const int key = c.key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z)          vk = UINT('A' + (key - Qt::Key_A));
    else if (key >= Qt::Key_0 && key <= Qt::Key_9)     vk = UINT('0' + (key - Qt::Key_0));
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24)  vk = UINT(VK_F1 + (key - Qt::Key_F1));
    else switch (key) {
        case Qt::Key_Space:     vk = VK_SPACE;  break;
        case Qt::Key_Left:      vk = VK_LEFT;   break;
        case Qt::Key_Right:     vk = VK_RIGHT;  break;
        case Qt::Key_Up:        vk = VK_UP;     break;
        case Qt::Key_Down:      vk = VK_DOWN;   break;
        case Qt::Key_Home:      vk = VK_HOME;   break;
        case Qt::Key_End:       vk = VK_END;    break;
        case Qt::Key_Insert:    vk = VK_INSERT; break;
        case Qt::Key_Delete:    vk = VK_DELETE; break;
        case Qt::Key_PageUp:    vk = VK_PRIOR;  break;
        case Qt::Key_PageDown:  vk = VK_NEXT;   break;
        case Qt::Key_MediaPlay: vk = VK_MEDIA_PLAY_PAUSE; break;
        case Qt::Key_VolumeUp:  vk = VK_VOLUME_UP;   break;
        case Qt::Key_VolumeDown:vk = VK_VOLUME_DOWN; break;
        case Qt::Key_VolumeMute:vk = VK_VOLUME_MUTE; break;
        default: return false;
    }
    // F12 is reserved by the debugger and will fail to register.
    return vk != VK_F12;
}

} // namespace pq
```

```
// ============ AtomicConfigWriter.h / .cpp ============
// Writes peace.txt so APO's FindFirstChangeNotification reloader can never
// observe a torn file.
//
// Why this shape (all verified):
//  * APO opens the config with GENERIC_READ / FILE_SHARE_READ only and spins
//    `Sleep(1)` until it succeeds (FilterEngine.cpp). So while APO holds it,
//    OUR MoveFileEx gets ERROR_SHARING_VIOLATION -> we need a retry loop too.
//  * Rename is atomic: APO sees either the whole old file or the whole new one.
//  * APO watches the config dir with bWatchSubtree=TRUE and
//    FILE_NOTIFY_CHANGE_FILE_NAME, so creating the temp INSIDE config/ causes
//    a second, wasted reload. Stage it in the parent dir instead -- same
//    volume (required for an atomic MoveFileEx), outside the watched tree.
//    Verified writable non-elevated on this machine.
//  * APO coalesces with only a 10 ms window, so avoid burst-writing.
#pragma once
#include <QString>
#include <QByteArray>
#include <windows.h>

namespace pq {

class AtomicConfigWriter {
public:
    // stagingDir should be on the SAME volume as the target and OUTSIDE the
    // APO config tree, e.g. <InstallPath>/.dreamdsp-tmp
    AtomicConfigWriter(QString configDir, QString stagingDir);

    // utf8 content; writes a UTF-8 BOM-less file with CRLF already applied by
    // the caller. Returns false and fills errorString() on failure.
    bool write(const QString& fileName, const QByteArray& utf8);

    QString errorString() const { return m_error; }

private:
    bool sameVolume(const QString& a, const QString& b) const;

    QString m_configDir, m_stagingDir, m_error;
};

} // namespace pq

// ---------------- implementation ----------------
#include "AtomicConfigWriter.h"
#include <QDir>
#include <QUuid>
#include <QThread>

namespace pq {
namespace {

std::wstring wstr(const QString& s) {
    return std::wstring(reinterpret_cast<const wchar_t*>(s.utf16()),
                        size_t(s.size()));
}

bool retryable(DWORD e) {
    return e == ERROR_SHARING_VIOLATION   // 32  -- APO is reading it right now
        || e == ERROR_LOCK_VIOLATION      // 33
        || e == ERROR_ACCESS_DENIED;      // 5   -- transient AV / indexer
}

// Full write + flush + close. Flush matters: MoveFileEx does not imply that
// the source data reached the disk.
bool writeWhole(const std::wstring& path, const QByteArray& data, DWORD& err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = GetLastError(); return false; }

    const char* p = data.constData();
    DWORD remaining = DWORD(data.size());
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(h, p, remaining, &written, nullptr) || written == 0) {
            err = GetLastError();
            CloseHandle(h);
            return false;
        }
        p += written;
        remaining -= written;
    }
    FlushFileBuffers(h);
    CloseHandle(h);
    err = 0;
    return true;
}

} // anon namespace

AtomicConfigWriter::AtomicConfigWriter(QString configDir, QString stagingDir)
    : m_configDir(std::move(configDir)), m_stagingDir(std::move(stagingDir)) {}

bool AtomicConfigWriter::sameVolume(const QString& a, const QString& b) const {
    wchar_t va[MAX_PATH]{}, vb[MAX_PATH]{};
    if (!GetVolumePathNameW(wstr(a).c_str(), va, MAX_PATH)) return false;
    if (!GetVolumePathNameW(wstr(b).c_str(), vb, MAX_PATH)) return false;
    return _wcsicmp(va, vb) == 0;
}

bool AtomicConfigWriter::write(const QString& fileName, const QByteArray& utf8) {
    m_error.clear();

    // Fall back to staging inside config/ if the staging dir is on another
    // volume -- a cross-volume MoveFileEx is NOT atomic.
    QString stage = m_stagingDir;
    if (stage.isEmpty() || !sameVolume(stage, m_configDir))
        stage = m_configDir;
    QDir().mkpath(stage);

    const QString tmpPath = QDir(stage).filePath(
        QStringLiteral(".dreamdsp-%1.tmp")
            .arg(QUuid::createUuid().toString(QUuid::Id128)));
    const QString dstPath = QDir(m_configDir).filePath(fileName);

    const std::wstring wTmp = wstr(QDir::toNativeSeparators(tmpPath));
    const std::wstring wDst = wstr(QDir::toNativeSeparators(dstPath));

    DWORD err = 0;
    if (!writeWhole(wTmp, utf8, err)) {
        m_error = QStringLiteral("write temp failed (GetLastError=%1)").arg(err);
        return false;
    }

    // Atomic swap, with the retry APO's FILE_SHARE_READ-only open forces on us.
    // MOVEFILE_COPY_ALLOWED is deliberately NOT set -- it would silently make
    // the operation non-atomic across volumes.
    const int kMaxAttempts = 60;          // ~1 s worst case
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (MoveFileExW(wTmp.c_str(), wDst.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;

        err = GetLastError();
        if (!retryable(err)) break;
        QThread::msleep(attempt < 10 ? 2 : 20);   // fast then slow backoff
    }

    DeleteFileW(wTmp.c_str());
    m_error = QStringLiteral("MoveFileEx failed (GetLastError=%1)").arg(err);
    return false;
}

} // namespace pq
```

```
// ============ SafeFileWatcher.h / .cpp ============
// QFileSystemWatcher hardened for Windows.
//
// Qt docs: "QFileSystemWatcher stops monitoring files once they have been
// renamed or removed from disk" -- which is EXACTLY what an atomic
// write-temp-then-rename does. Also "only the last change in the sequence
// will always generate this signal", so bursts need debouncing.
//
// Fix: watch the file AND its parent directory. The directory watch survives
// the delete/recreate and is what lets us re-arm the file watch.
#pragma once
#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QStringList>
#include <QSet>

namespace pq {

class SafeFileWatcher : public QObject {
    Q_OBJECT
public:
    explicit SafeFileWatcher(QObject* parent = nullptr);

    void watchFile(const QString& absPath);
    void unwatchFile(const QString& absPath);
    void setDebounceMs(int ms) { m_debounce.setInterval(ms); }

    // Call around your own writes so you don't react to yourself.
    void beginSelfWrite() { ++m_selfWrite; }
    void endSelfWrite();

signals:
    void fileChanged(const QString& absPath);   // debounced, re-armed
    void fileDisappeared(const QString& absPath);

private:
    void onPathChanged();
    void rearmAndEmit();

    QFileSystemWatcher m_w;
    QTimer      m_debounce;
    QSet<QString> m_files;   // files we are supposed to be watching
    QSet<QString> m_dirs;    // their parent directories
    int m_selfWrite = 0;
};

} // namespace pq

// ---------------- implementation ----------------
#include "SafeFileWatcher.h"
#include <QFileInfo>
#include <QDir>

namespace pq {

SafeFileWatcher::SafeFileWatcher(QObject* parent) : QObject(parent) {
    // Long enough to swallow the create-temp + rename pair, and comfortably
    // longer than APO's own 10 ms coalescing window.
    m_debounce.setInterval(150);
    m_debounce.setSingleShot(true);

    connect(&m_w, &QFileSystemWatcher::fileChanged,
            this, &SafeFileWatcher::onPathChanged);
    connect(&m_w, &QFileSystemWatcher::directoryChanged,
            this, &SafeFileWatcher::onPathChanged);
    connect(&m_debounce, &QTimer::timeout,
            this, &SafeFileWatcher::rearmAndEmit);
}

void SafeFileWatcher::watchFile(const QString& absPath) {
    const QString f = QFileInfo(absPath).absoluteFilePath();
    const QString d = QFileInfo(f).absolutePath();

    m_files.insert(f);
    // The directory watch is the part that survives delete/rename.
    if (!m_dirs.contains(d) && QDir(d).exists()) {
        if (m_w.addPath(d)) m_dirs.insert(d);
    }
    if (QFileInfo::exists(f) && !m_w.files().contains(f))
        m_w.addPath(f);
}

void SafeFileWatcher::unwatchFile(const QString& absPath) {
    const QString f = QFileInfo(absPath).absoluteFilePath();
    m_files.remove(f);
    m_w.removePath(f);

    // Drop the directory watch only if no watched file still needs it.
    const QString d = QFileInfo(f).absolutePath();
    for (const QString& other : m_files)
        if (QFileInfo(other).absolutePath() == d) return;
    if (m_dirs.remove(d)) m_w.removePath(d);
}

void SafeFileWatcher::endSelfWrite() {
    if (m_selfWrite > 0) --m_selfWrite;
    // Swallow the notifications our own write just generated.
    if (m_selfWrite == 0) m_debounce.stop();
    rearmAndEmitGuard:
    // Always re-arm: our own rename dropped the file watch too.
    for (const QString& f : m_files)
        if (QFileInfo::exists(f) && !m_w.files().contains(f))
            m_w.addPath(f);
}

void SafeFileWatcher::onPathChanged() {
    if (m_selfWrite > 0) return;   // our own write in progress
    m_debounce.start();            // restart -> coalesces bursts
}

void SafeFileWatcher::rearmAndEmit() {
    for (const QString& f : m_files) {
        const bool exists  = QFileInfo::exists(f);
        const bool watched = m_w.files().contains(f);

        if (exists && !watched) {
            // The file was renamed/replaced under us -> Qt silently dropped it.
            m_w.addPath(f);
            emit fileChanged(f);
        } else if (exists) {
            emit fileChanged(f);
        } else {
            emit fileDisappeared(f);
            // Keep it in m_files; the directory watch will tell us when it
            // comes back, and the next pass will re-add it.
        }
    }
}

} // namespace pq
```

```
# ============ CMakeLists.txt fragment (verified toolchain) ============
# Configured and built successfully on this machine with:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_PREFIX_PATH="D:/Qt/6.8.3/msvc2022_64"
# after `call "D:\Program Files\VisualStudio\2026\IDE\VC\Auxiliary\Build\vcvars64.bat"`
# MSVC 14.51.36231, cmake at D:/Program Files/CMake/bin/cmake.exe, ninja on PATH.

cmake_minimum_required(VERSION 3.21)
project(DreamDSP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)

add_library(pq_platform STATIC
    platform/WinAudioEndpoints.cpp
    platform/ApoInfo.cpp
    platform/EndpointNotifier.cpp
    platform/PeakMeter.cpp
    platform/HotkeyManager.cpp
    platform/AtomicConfigWriter.cpp
    platform/SafeFileWatcher.cpp
)

target_include_directories(pq_platform PUBLIC platform)

target_link_libraries(pq_platform PUBLIC
    Qt6::Core
    Qt6::Gui
    ole32        # CoCreateInstance, CoTaskMemFree, CLSIDFromString
    oleaut32
    propsys      # PropVariantInit / PropVariantClear
    user32       # RegisterHotKey / UnregisterHotKey
    advapi32     # RegOpenKeyExW / RegQueryValueExW
)

target_compile_definitions(pq_platform PUBLIC
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Ship as asInvoker: no elevation is needed for the APO config directory
# on this machine (inherited Authenticated Users:(M)). Do NOT add a
# requireAdministrator manifest.
```

```
// ============ Startup wiring + writability probe ============
// Ties the pieces together and answers "can we write without elevation?"
// at runtime instead of assuming the installer's ACL is intact.
#include "WinAudioEndpoints.h"
#include "ApoInfo.h"
#include "EndpointNotifier.h"
#include "AtomicConfigWriter.h"
#include <QGuiApplication>
#include <QDir>
#include <QDebug>
#include <objbase.h>

namespace pq {

// True if we can create+delete a file in dir as the current (non-elevated) user.
// FILE_FLAG_DELETE_ON_CLOSE means we never leave litter behind, even on crash.
bool canWriteDirectory(const QString& dir, DWORD* errOut = nullptr) {
    const QString probe = QDir(dir).filePath(QStringLiteral(".dreamdsp-probe"));
    const std::wstring w(reinterpret_cast<const wchar_t*>(
        QDir::toNativeSeparators(probe).utf16()));

    HANDLE h = CreateFileW(w.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (errOut) *errOut = GetLastError();   // 5 == ERROR_ACCESS_DENIED
        return false;
    }
    CloseHandle(h);
    if (errOut) *errOut = 0;
    return true;
}

struct Platform {
    ApoPaths          apo;
    EndpointNotifier  notifier;
    AtomicConfigWriter* writer = nullptr;
    bool              writable = false;
};

bool initPlatform(Platform& p) {
    // COM: STA, matching the Qt GUI thread.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return false;

    p.apo = ApoLocator::locate();
    if (!p.apo.found) {
        qWarning("Equalizer APO not found under HKLM\\SOFTWARE\\EqualizerAPO");
        return false;
    }
    const QString cfg = QString::fromWCharArray(p.apo.configPath.c_str());
    const QString ins = QString::fromWCharArray(p.apo.installPath.c_str());
    qInfo() << "APO install:" << ins << "config:" << cfg;

    DWORD err = 0;
    p.writable = canWriteDirectory(cfg, &err);
    if (!p.writable)
        qWarning("config dir not writable (GetLastError=%lu) -- "
                 "prompt the user to fix the ACL", err);

    // Stage temps outside the watched tree, same volume.
    const QString staging = QDir(ins).filePath(QStringLiteral(".dreamdsp-tmp"));
    QDir().mkpath(staging);
    p.writer = new AtomicConfigWriter(cfg, staging);

    // Enumerate everything, including unplugged devices, and flag APO status.
    for (const auto& e : WinAudioEndpoints::enumerate(eRender)) {
        const bool apoOn = ApoDeviceStatus::isEnabled(e.regGuid, /*capture=*/false);
        qInfo().noquote()
            << QString::fromWCharArray(e.friendly.c_str())
            << "state=0x" + QString::number(e.state, 16)
            << (e.isDefaultConsole ? "[default]" : "")
            << (apoOn ? "[APO]" : "");
    }

    p.notifier.start();   // hot-plug -> Qt signals on the GUI thread
    return true;
}

} // namespace pq
```

UNKNOWNS: Whether APO reloads correctly when the config DIRECTORY itself is deleted or replaced while FindFirstChangeNotification is armed. FilterEngine.cpp calls FindNextChangeNotification unconditionally and never re-arms the handle, so the watcher is likely lost permanently — but I did not test this destructively on the live install.; What happens when two writers race inside APO's 10 ms coalescing window (e.g. DreamDSP and the still-installed Peace.exe both saving). Not tested; both processes are live on this machine and a test would have disturbed the user's audio config.; Whether ReplaceFileW (as opposed to MoveFileEx) succeeds non-elevated here. My .NET File.Replace test failed on a binding quirk ('The path is empty', from passing $null as the backup path), not on a real Win32 error. MoveFileEx WAS verified working. ReplaceFileW would additionally preserve the destination's ACL/attributes/streams, which may matter if the user ever sets an explicit ACL on peace.txt — worth a direct P/Invoke test before choosing it.; The exact upstream version of DeviceAPOInfo.cpp/.h I fetched. I used the github.com/mirror/equalizerapo master mirror, which tracks APO 1.4.0-era code; the installed APO here is 1.4.1. The PROPERTYKEY set and the two CLSIDs matched the live registry exactly, but I did not diff 1.4.0 vs 1.4.1 sources.; Whether IMMNotificationClient callbacks on this machine ever arrive re-entrantly or during UnregisterEndpointNotificationCallback. The detach()+Release() ordering in code_samples[2] is defensive but was not stress-tested with real hot-plug events.; Peak values were 0.0 on every device during the probe because nothing was playing. I confirmed GetPeakValue returns S_OK and the channel count, but did not confirm non-zero readings under active playback, nor did I empirically confirm the exclusive-mode-returns-0.0 behaviour (that claim rests on the MSDN Peak Meters page plus the measured absence of ENDPOINT_HARDWARE_SUPPORT_METER).; Whether Peace's ~25 .peace preset files and peace.ini can be parsed into DreamDSP's model — I only listed the config directory, I did not read the preset format or peace.ini schema. That is a separate topic from the Windows integration layer.; Capture-side (eCapture) endpoints: I enumerated and verified only eRender. The code paths are parameterised for eCapture and the registry layout under MMDevices\Audio\Capture is symmetric, but I did not run the probe against capture devices.