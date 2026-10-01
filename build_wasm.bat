@echo off
REM Build the WASM demo: compiles the interpreter + scenario shim into
REM docs/evm.js + docs/evm.wasm (served by GitHub Pages from /docs).
REM Requires emsdk at %USERPROFILE%\emsdk.
setlocal
set "EMSDK=%USERPROFILE%\emsdk"
set "EMSDK_NODE=%EMSDK%\node\24.19.0_64bit\node.exe"
set "EMSDK_PYTHON=%EMSDK%\python\3.13.3_64bit\python.exe"
set "EMCC=%EMSDK%\upstream\emscripten\em++.exe"
if not exist "%EMCC%" (
    echo emsdk not found at %EMSDK%
    exit /b 1
)
"%EMCC%" -O2 -std=c++20 -fexceptions ^
    -Iinclude -Itests ^
    src\uint256.cpp src\evm.cpp src\host.cpp src\opcode.cpp src\hex.cpp src\keccak.cpp ^
    tools\demo\scenario.cpp ^
    -s MODULARIZE=1 -s EXPORT_NAME=createEvm ^
    -s ALLOW_MEMORY_GROWTH=1 -s STACK_SIZE=4MB ^
    -s "EXPORTED_FUNCTIONS=['_evm_run_scenario']" ^
    -s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','HEAPU8']" ^
    -o docs\evm.js
if errorlevel 1 exit /b 1
echo Built docs\evm.js + docs\evm.wasm
endlocal
