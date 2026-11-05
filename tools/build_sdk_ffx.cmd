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
rem SDK_VARIANT=test: experimental defines (SDK_TEST_DEFINES, e.g.
rem -DAMPED3_EDRAM_2048) in their own build tree, copied only to build_test\ - the
rem playable build\ and project\sdk-bin keep the normal DLLs.
set TREE=out\build\win-amd64-v3
set DEST=build
set DEFINES=
if /i "%SDK_VARIANT%"=="test" (
  set TREE=out\build\win-amd64-v3-test
  set DEST=build_test
  set DEFINES=%SDK_TEST_DEFINES%
)
cd /d "%SRC%"
rem SDK_RELEASE=1: the DLLs for a release zip (no line tables or PDB link).
rem SDK_DLSS=1: NVIDIA DLSS built in (DLAA, withdrawn; off by default so a
rem release never carries it).
set DBG=-gcodeview -gline-tables-only
set LDBG=-g
if "%SDK_RELEASE%"=="1" (
  set DBG=
  set LDBG=
)
set DLSS=OFF
if "%SDK_DLSS%"=="1" set DLSS=ON
cmake --preset win-amd64 -B %TREE% -DCMAKE_C_COMPILER="%LLVM%\clang.exe" -DCMAKE_CXX_COMPILER="%LLVM%\clang++.exe" "-DCMAKE_C_FLAGS=-march=x86-64-v3 %DBG%" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3 %DBG% %DEFINES%" "-DCMAKE_SHARED_LINKER_FLAGS=%LDBG%" -DREXGLUE_ENABLE_FIDELITYFX=ON -DREXGLUE_ENABLE_DLSS=%DLSS% || exit /b 1
cmake --build %TREE% --config Release --target rexruntime rexgpu-xenos --parallel %1 || exit /b 1
for %%d in (rexruntime rexgpu-xenos) do (
  copy /y out\win-amd64\Release\%%d.dll "%~dp0..\%DEST%\%%d.dll" >nul || exit /b 1
)
rem AMD's signed FidelityFX DLL (same API; also has FSR 3 frame generation, which
rem our build of it leaves out).
set FFX_DLL=%TREE%\_deps\fidelityfx-src\PrebuiltSignedDLL\amd_fidelityfx_dx12.dll
if not exist "%FFX_DLL%" set FFX_DLL=bin\amd_fidelityfx_dx12.dll
copy /y "%FFX_DLL%" "%~dp0..\%DEST%\" >nul || exit /b 1
rem NVIDIA DLSS runtime (fetched by cmake/rexglue_dlss.cmake; NVIDIA RTX SDKs licence).
if exist %TREE%\_deps\dlss_sdk\bin\nvngx_dlss.dll copy /y %TREE%\_deps\dlss_sdk\bin\nvngx_dlss.dll "%~dp0..\%DEST%\" >nul
if /i "%SDK_VARIANT%"=="test" goto done
rem Also keep them in project\sdk-bin, which the game build copies next to the exe.
for %%d in (out\win-amd64\Release\rexruntime.dll out\win-amd64\Release\rexgpu-xenos.dll "%FFX_DLL%") do (
  copy /y %%d "%~dp0..\project\sdk-bin\" >nul || exit /b 1
)
:done
echo SDK FFX BUILD OK
