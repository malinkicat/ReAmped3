@echo off
rem Configure (first run) and build the ThinLTO experiment into build_lto\ (-DAMPED3_LTO=ON).
cd /d "%~dp0.."
set ROOT=%CD%
if exist "%~dp0local.cmd" call "%~dp0local.cmd"
if "%LLVM%"=="" set LLVM=C:\llvm\bin
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=%LLVM%;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
if not exist build_lto\build.ninja (
  cmake -S project -B build_lto -G Ninja -DCMAKE_C_COMPILER="%LLVM%\clang.exe" -DCMAKE_CXX_COMPILER="%LLVM%\clang++.exe" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%ROOT%\.tools\sdk\win-amd64" -DAMPED3_LTO=ON "-DAMPED3_PGO=%ROOT%\pgo\amped3.profdata" || exit /b 1
)
cmake --build build_lto --parallel %1 || exit /b 1
echo BUILD OK
