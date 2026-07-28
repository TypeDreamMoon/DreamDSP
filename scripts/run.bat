@echo off
:: Run DreamDSP from the build tree. Qt and HuskarUI DLLs are picked up from
:: PATH rather than deployed, which keeps the edit-build-run loop fast.
setlocal
call "%~dp0env.bat" || exit /b 1

set "BUILD_DIR=%PROJECT_ROOT%\build\dreamdsp"
set "PATH=%QT_DIR%\bin;%HUSKARUI_PREFIX%\bin;%PATH%"

if not exist "%BUILD_DIR%\DreamDSP.exe" (
    echo [dreamdsp] not built yet -- run scripts\build.bat
    exit /b 1
)

"%BUILD_DIR%\DreamDSP.exe" %*
