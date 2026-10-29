@echo off
rem Same as build_sdk_v3.cmd (AVX2) with AMD FidelityFX on: present_effect=fsr/cas/fsr2/fsr3.
rem The first configure fetches FidelityFX-SDK from GitHub. Line tables + PDBs (no codegen change)
rem let tools/cpu_profile.py symbolize samples.
if exist "%~dp0local.cmd" call "%~dp0local.cmd"
if "%SDK_SRC%"=="" (echo Set SDK_SRC in tools\local.cmd ^(the ReXGlue source tree^) & exit /b 1)
set SRC=%SDK_SRC%
if "%LLVM%"=="" set LLVM=C:\llvm\bin
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=%LLVM%;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
cd /d "%SRC%"
cmake --preset win-amd64 -B out\build\win-amd64-v3 -DCMAKE_C_COMPILER="%LLVM%\clang.exe" -DCMAKE_CXX_COMPILER="%LLVM%\clang++.exe" "-DCMAKE_C_FLAGS=-march=x86-64-v3 -gcodeview -gline-tables-only" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3 -gcodeview -gline-tables-only" -DCMAKE_SHARED_LINKER_FLAGS=-g -DREXGLUE_ENABLE_FIDELITYFX=ON || exit /b 1
cmake --build out\build\win-amd64-v3 --config Release --target rexruntime rexgpu-xenos --parallel %1 || exit /b 1
for %%d in (rexruntime rexgpu-xenos) do (
  copy /y out\win-amd64\Release\%%d.dll "%~dp0..\build\%%d.dll" >nul || exit /b 1
)
copy /y bin\amd_fidelityfx_dx12.dll "%~dp0..\build\" >nul || exit /b 1
rem Also keep them in project\sdk-bin, which the game build copies next to the exe.
for %%d in (out\win-amd64\Release\rexruntime.dll out\win-amd64\Release\rexgpu-xenos.dll bin\amd_fidelityfx_dx12.dll) do (
  copy /y %%d "%~dp0..\project\sdk-bin\" >nul || exit /b 1
)
:done
echo SDK FFX BUILD OK
