@echo off
rem Experiment: the runtime with both the D3D12 and Vulkan backends (Vulkan is off
rem on Windows by default), AVX2, no FidelityFX. Separate build tree and output
rem folder (%SDK_SRC%\out\vk-bin); nothing is copied into build\ or sdk-bin\.
rem The exe picks the backend with --gpu_backend=vulkan.
if exist "%~dp0local.cmd" call "%~dp0local.cmd"
if "%SDK_SRC%"=="" (echo Set SDK_SRC in tools\local.cmd ^(the ReXGlue source tree^) & exit /b 1)
set SRC=%SDK_SRC%
if "%LLVM%"=="" set LLVM=C:\llvm\bin
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=%LLVM%;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
cd /d "%SRC%"
cmake --preset win-amd64 -B out\build\win-amd64-vk -DCMAKE_C_COMPILER="%LLVM%\clang.exe" -DCMAKE_CXX_COMPILER="%LLVM%\clang++.exe" "-DCMAKE_C_FLAGS=-march=x86-64-v3 -gcodeview -gline-tables-only" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3 -gcodeview -gline-tables-only" -DCMAKE_SHARED_LINKER_FLAGS=-g -DREXGLUE_USE_VULKAN=ON -DREXGLUE_ENABLE_FIDELITYFX=OFF "-DREX_OUTPUT_DIR=%SRC%\out\vk-bin" || exit /b 1
cmake --build out\build\win-amd64-vk --config Release --target rexruntime rexgpu-xenos --parallel %1 || exit /b 1
echo SDK VK BUILD OK
