@echo off
:: Build HuskarUI and install it into third_party/HuskarUI-install so that
:: DreamDSP can consume it via find_package(HuskarUI). We deliberately do NOT
:: install into the Qt SDK directory (INSTALL_HUSKARUI_IN_DEFAULT_LOCATION=OFF)
:: to keep the Qt installation pristine.
setlocal
call "%~dp0env.bat" || exit /b 1

set "BUILD_DIR=%PROJECT_ROOT%\build\huskarui"
set "GALLERY=OFF"
if /i "%~1"=="gallery" set "GALLERY=ON"

echo [huskarui] configuring (gallery=%GALLERY%) ...
cmake -G Ninja -S "%HUSKARUI_SRC%" -B "%BUILD_DIR%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_INSTALL_PREFIX="%HUSKARUI_PREFIX%" ^
    -DBUILD_HUSKARUI_GALLERY=%GALLERY% ^
    -DINSTALL_HUSKARUI_IN_DEFAULT_LOCATION=OFF || exit /b 1

echo [huskarui] building ...
cmake --build "%BUILD_DIR%" --parallel || exit /b 1

echo [huskarui] installing to "%HUSKARUI_PREFIX%" ...
cmake --install "%BUILD_DIR%" || exit /b 1

echo [huskarui] done.
