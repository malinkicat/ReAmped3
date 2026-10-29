@echo off
rem Build the launcher (project\launcher_sdl: SDL3 + Dear ImGui, linked statically)
rem into build\amped3_launcher.exe. The first configure downloads SDL3 and Dear ImGui.
rem The earlier WPF launcher (project\launcher\Launcher.cs) is no longer built.
cd /d "%~dp0.."
if exist "%~dp0local.cmd" call "%~dp0local.cmd"
if "%LLVM%"=="" set LLVM=C:\llvm\bin
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=%LLVM%;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
if not exist build\launcher\build.ninja (
  cmake -S project\launcher_sdl -B build\launcher -G Ninja -DCMAKE_C_COMPILER="%LLVM%\clang.exe" -DCMAKE_CXX_COMPILER="%LLVM%\clang++.exe" -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
cmake --build build\launcher --parallel %1 || exit /b 1
copy /y build\launcher\amped3_launcher.exe build\amped3_launcher.exe >nul || exit /b 1
build\amped3_launcher.exe --selftest | findstr /x /c:"SELFTEST OK" >nul || (echo launcher selftest failed & exit /b 1)
echo LAUNCHER OK
