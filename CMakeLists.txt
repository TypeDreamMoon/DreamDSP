cmake_minimum_required(VERSION 3.21)

project(DreamDSP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Quick Network)
find_package(HuskarUI REQUIRED)

qt_standard_project_setup(REQUIRES 6.7)

# The DSP layer and its offline exerciser are independent of Qt entirely.
add_subdirectory(dsp)
add_subdirectory(harness)

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
        qml/components/EqualizerPane.qml
        qml/components/EffectsPane.qml
    SOURCES
        src/core/Biquad.h              src/core/Biquad.cpp
        src/core/ApoConfig.h           src/core/ApoConfig.cpp
        src/core/PeacePreset.h         src/core/PeacePreset.cpp
        src/core/AutoEqDatabase.h      src/core/AutoEqDatabase.cpp
        src/core/Fft.h                 src/core/Fft.cpp
        src/core/ImpulseResponse.h     src/core/ImpulseResponse.cpp
        src/core/AutoEqDatabase.h      src/core/AutoEqDatabase.cpp
        src/platform/ApoLocator.h      src/platform/ApoLocator.cpp
        src/platform/AudioDevices.h    src/platform/AudioDevices.cpp
        src/platform/TrayIcon.h        src/platform/TrayIcon.cpp
        src/platform/Autostart.h       src/platform/Autostart.cpp
        src/platform/HotkeyManager.h   src/platform/HotkeyManager.cpp
        src/platform/PeakMeter.h       src/platform/PeakMeter.cpp
        src/platform/SingleInstance.h  src/platform/SingleInstance.cpp
        src/platform/LoopbackCapture.h src/platform/LoopbackCapture.cpp
        src/app/EqBandModel.h          src/app/EqBandModel.cpp
        src/app/PresetStore.h          src/app/PresetStore.cpp
        src/app/AppController.h        src/app/AppController.cpp
        src/app/SelfTest.h             src/app/SelfTest.cpp
        src/ui/ResponseCurveItem.h     src/ui/ResponseCurveItem.cpp
        src/ui/TransferCurveItem.h     src/ui/TransferCurveItem.cpp
        src/app/CompressorModel.h      src/app/CompressorModel.cpp
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
    DREAMDSP_VERSION="${PROJECT_VERSION}"
    UNICODE _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(DreamDSP PRIVATE
    Qt6::Quick
    Qt6::Network   # QLocalServer/Socket for the single-instance guard
    HuskarUI::Basic
    dreamdsp_core  # the host-neutral DSP layer
    ole32       # COM / MMDevice
    uuid        # PKEY_* / IID_* GUIDs
    advapi32    # registry
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
