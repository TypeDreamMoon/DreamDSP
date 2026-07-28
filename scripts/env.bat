@echo off
:: Shared build environment for DreamDSP.
:: Qt 6.8.3 msvc2022_64 + VS2022 toolchain + Qt's bundled Ninja.

set "QT_DIR=D:\Qt\6.8.3\msvc2022_64"
set "VCVARS=D:\Program Files\VisualStudio\2022\IDE\VC\Auxiliary\Build\vcvars64.bat"
set "NINJA=D:\Qt\Tools\Ninja\ninja.exe"

set "PROJECT_ROOT=I:\Qt\DreamDSP"
set "HUSKARUI_SRC=%PROJECT_ROOT%\third_party\HuskarUI"
set "HUSKARUI_PREFIX=%PROJECT_ROOT%\third_party\HuskarUI-install"

if not exist "%VCVARS%" (
    echo [env] ERROR: vcvars64.bat not found at "%VCVARS%"
    exit /b 1
)
if not exist "%QT_DIR%\bin\qmake.exe" (
    echo [env] ERROR: Qt not found at "%QT_DIR%"
    exit /b 1
)

:: vcvars is chatty and re-entrant-unsafe; only run it once per shell.
if not defined VSCMD_ARG_TGT_ARCH call "%VCVARS%" >nul
