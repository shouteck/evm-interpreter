@echo off
setlocal

set "VSROOT=C:\Program Files\Microsoft Visual Studio\18\Community"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul

set "CMAKEBIN=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJABIN=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
set "PATH=%CMAKEBIN%;%NINJABIN%;%PATH%"

if not exist build\CMakeCache.txt (
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
cmake --build build || exit /b 1

if /i "%~1"=="test"  ctest --test-dir build --output-on-failure
