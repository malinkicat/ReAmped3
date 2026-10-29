@echo off
rem Profile-guided optimization of the game code.
rem   tools\build_pgo.cmd generate   instrumented exe in build_pgo\ (run it, then the scripted
rem                                  "pgodump" command writes build_pgo\default.profraw)
rem   tools\build_pgo.cmd use        merge the .profraw files into pgo\amped3.profdata and
rem                                  rebuild build\ with it (tools\build.cmd keeps using it)
cd /d "%~dp0.."
set ROOT=%CD%
if exist "%~dp0local.cmd" call "%~dp0local.cmd"
if "%LLVM%"=="" set LLVM=C:\llvm\bin
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=%LLVM%;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
if "%1"=="generate" (
  cmake -S project -B build_pgo -G Ninja -DCMAKE_C_COMPILER="%LLVM%\clang.exe" -DCMAKE_CXX_COMPILER="%LLVM%\clang++.exe" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%ROOT%\.tools\sdk\win-amd64" -DAMPED3_PGO=generate || exit /b 1
  cmake --build build_pgo --parallel %2 || exit /b 1
  echo PGO GENERATE BUILD OK
  exit /b 0
)
if "%1"=="use" (
  if not exist pgo mkdir pgo
  "%LLVM%\llvm-profdata.exe" merge -o pgo\amped3.profdata build_pgo\*.profraw || exit /b 1
  cmake -S project -B build -DAMPED3_PGO="%ROOT%\pgo\amped3.profdata" || exit /b 1
  cmake --build build --parallel %2 || exit /b 1
  echo PGO USE BUILD OK
  exit /b 0
)
echo usage: tools\build_pgo.cmd generate^|use
exit /b 1
