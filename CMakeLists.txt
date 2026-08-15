cmake_minimum_required(VERSION 3.21)

project(DreamDSP VERSION 0.2.0 LANGUAGES CXX)

# The version comes from the git tag, not from the line above -- that one is
# only the fallback for a tree with no tags at all. See cmake/Version.cmake.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Version)
dreamdsp_version_from_git("${PROJECT_VERSION}")
if(DREAMDSP_IS_RELEASE)
    message(STATUS "DreamDSP version ${DREAMDSP_VERSION_FULL} (release)")
else()
    message(STATUS "DreamDSP version ${DREAMDSP_VERSION_FULL}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Quick Network Concurrent)
find_package(HuskarUI REQUIRED)

qt_standard_project_setup(REQUIRES 6.7)

# The DSP layer and its offline exerciser are independent of Qt entirely.
add_subdirectory(dsp)
add_subdirectory(harness)
add_subdirectory(apo)

# HuskarUIConfig.cmake gives us HuskarUI_INCLUDE_DIR / HuskarUI_QML_IMPORT_PATH.
# The runtime DLLs live next to them in <prefix>/bin.
get_filename_component(HUSKARUI_ROOT "${HuskarUI_INCLUDE_DIR}" DIRECTORY)
set(HUSKARUI_BIN_DIR "${HUSKARUI_ROOT}/bin")

qt_add_executable(DreamDSP
    src/main.cpp
)

qt_add_qml_module(DreamDSP
    URI DreamDSP
    VERSION 1.0
    RESOURCE_PREFIX "/"
    QML_FILES
        qml/Main.qml
        qml/components/BandStrip.qml
        qml/components/SectionCard.qml
        qml/components/StatusPill.qml
        qml/components/PresetBar.qml
        qml/components/BandTable.qml
        qml/components/TrayMenu.qml
        qml/components/SettingsPane.qml
        qml/components/AutoEqPane.qml
        qml/components/MeterBar.qml
        qml/components/ConvolutionPane.qml
        qml/components/NavRail.qml
        qml/components/Fader.qml
        qml/components/EqualizerPane.qml
        qml/components/EffectsPane.qml
        qml/components/OutputPane.qml
        qml/components/ChainPane.qml
    SOURCES
        src/core/Biquad.h              src/core/Biquad.cpp
        src/core/ApoConfig.h           src/core/ApoConfig.cpp
        src/core/PeacePreset.h         src/core/PeacePreset.cpp
        src/core/DreamPreset.h         src/core/DreamPreset.cpp
        src/core/GraphicCurve.h        src/core/GraphicCurve.cpp
        src/core/AutoEqDatabase.h      src/core/AutoEqDatabase.cpp
        src/core/Fft.h                 src/core/Fft.cpp
        src/core/ImpulseResponse.h     src/core/ImpulseResponse.cpp
        src/core/AutoEqDatabase.h      src/core/AutoEqDatabase.cpp
        src/platform/ApoLocator.h      src/platform/ApoLocator.cpp
        src/platform/ApoInstaller.h    src/platform/ApoInstaller.cpp
        src/platform/AudioDevices.h    src/platform/AudioDevices.cpp
        src/platform/TrayIcon.h        src/platform/TrayIcon.cpp
        src/platform/Autostart.h       src/platform/Autostart.cpp
        src/platform/HotkeyManager.h   src/platform/HotkeyManager.cpp
        src/platform/PeakMeter.h       src/platform/PeakMeter.cpp
        src/platform/VolumeReader.h    src/platform/VolumeReader.cpp
        src/platform/SingleInstance.h  src/platform/SingleInstance.cpp
        src/platform/LoopbackCapture.h src/platform/LoopbackCapture.cpp
        src/platform/TonePlayer.h      src/platform/TonePlayer.cpp
        src/platform/UpdateChecker.h   src/platform/UpdateChecker.cpp
        src/app/EqBandModel.h          src/app/EqBandModel.cpp
        src/app/PresetStore.h          src/app/PresetStore.cpp
        src/app/AppController.h        src/app/AppController.cpp
        src/app/SelfTest.h             src/app/SelfTest.cpp
        src/ui/ResponseCurveItem.h     src/ui/ResponseCurveItem.cpp
        src/ui/TransferCurveItem.h     src/ui/TransferCurveItem.cpp
        src/ui/ImpulseCurveItem.h      src/ui/ImpulseCurveItem.cpp
        src/app/CompressorModel.h      src/app/CompressorModel.cpp
        src/app/ReverbModel.h          src/app/ReverbModel.cpp
        src/app/OfflineRender.h        src/app/OfflineRender.cpp
        src/app/EffectsModel.h         src/app/EffectsModel.cpp
        src/app/OutputModel.h          src/app/OutputModel.cpp
        src/app/ParamPublisher.h       src/app/ParamPublisher.cpp
)

# qmltyperegistrar emits `#include <AppController.h>` using the bare header
# basename, guarded by __has_include -- so every directory holding a QML_ELEMENT
# header must be on the include path, or the type silently fails to compile.
target_include_directories(DreamDSP PRIVATE
    src
    src/core
    src/app
    src/platform
    src/ui
)

target_compile_definitions(DreamDSP PRIVATE
    HUSKARUI_IMPORT_PATH="${HuskarUI_QML_IMPORT_PATH}"
    DREAMDSP_VERSION="${DREAMDSP_VERSION}"
    DREAMDSP_VERSION_FULL="${DREAMDSP_VERSION_FULL}"
    DREAMDSP_REPO="TypeDreamMoon/DreamDSP"
    UNICODE _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(DreamDSP PRIVATE
    Qt6::Quick
    Qt6::Network      # QLocalServer/Socket for the single-instance guard
    Qt6::Concurrent   # offline rendering off the GUI thread
    HuskarUI::Basic
    dreamdsp_core  # the host-neutral DSP layer
    ole32       # COM / MMDevice
    uuid        # PKEY_* / IID_* GUIDs
    advapi32    # registry, service control manager
    shell32     # ShellExecuteEx, for the one elevation prompt the APO needs
    user32      # RegisterHotKey (later)
)

set_target_properties(DreamDSP PROPERTIES WIN32_EXECUTABLE TRUE)

# HuskarUI ships as shared libs; drop them beside the executable so the app
# runs straight out of the build tree without touching PATH.
add_custom_command(TARGET DreamDSP POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${HUSKARUI_BIN_DIR}/HuskarUIBasic.dll"
        "${HUSKARUI_BIN_DIR}/HuskarUIImpl.dll"
        "$<TARGET_FILE_DIR:DreamDSP>"
    COMMENT "Copying HuskarUI runtime"
)
