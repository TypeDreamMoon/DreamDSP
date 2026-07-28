@echo off
:: Configure + build DreamDSP. Pass "clean" to wipe the build directory first.
setlocal
call "%~dp0env.bat" || exit /b 1

set "BUILD_DIR=%PROJECT_ROOT%\build\dreamdsp"

if /i "%~1"=="clean" (
    echo [dreamdsp] wiping "%BUILD_DIR%"
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

if not exist "%HUSKARUI_PREFIX%\lib\cmake\HuskarUI\HuskarUIConfig.cmake" (
    echo [dreamdsp] HuskarUI is not installed yet -- run scripts\build-huskarui.bat first.
    exit /b 1
)

:: There is an older HuskarUI (0.5.2) installed into the Qt SDK itself. Point
:: HuskarUI_DIR straight at our own 0.7.0 build so find_package cannot pick the
:: stale one up -- CMAKE_PREFIX_PATH ordering alone is too easy to get wrong.
:: RelWithDebInfo, not Debug: HuskarUI is built Release (/MD) and Qt refuses to
:: load a plugin built against a different CRT/Qt flavour. RelWithDebInfo uses
:: the release runtime while still producing usable debug info.
cmake -G Ninja -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DHuskarUI_DIR="%HUSKARUI_PREFIX%\lib\cmake\HuskarUI" || exit /b 1

cmake --build "%BUILD_DIR%" --parallel || exit /b 1

echo [dreamdsp] built: "%BUILD_DIR%\DreamDSP.exe"
