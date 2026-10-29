@echo off
rem Build the patched ReXGlue runtime and GPU plugin from %SDK_SRC% and copy
rem them next to amped3.exe. Apply tools\patch_sdk_*.py to the source first.
if exist "%~dp0local.cmd" call "%~dp0local.cmd"
if "%SDK_SRC%"=="" (echo Set SDK_SRC in tools\local.cmd & exit /b 1)
set SRC=%SDK_SRC%
if "%LLVM%"=="" set LLVM=C:\llvm\bin
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=%LLVM%;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
cd /d "%SRC%"
if not exist out\build\win-amd64\build.ninja (
  cmake --preset win-amd64 -DCMAKE_C_COMPILER="%LLVM%\clang.exe" -DCMAKE_CXX_COMPILER="%LLVM%\clang++.exe" || exit /b 1
)
cmake --build out\build\win-amd64 --config Release --target rexruntime rexgpu-xenos --parallel %1 || exit /b 1
for %%d in (rexruntime rexgpu-xenos) do (
  copy /y out\win-amd64\Release\%%d.dll "%~dp0..\build\%%d.dll" >nul || exit /b 1
)
echo SDK BUILD OK
