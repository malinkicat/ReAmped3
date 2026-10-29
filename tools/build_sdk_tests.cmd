@echo off
rem Build and run the ReXGlue fork's unit tests (separate build tree, tests on):
rem   tools\build_sdk_tests.cmd ["catch2 test filter"]
if exist "%~dp0local.cmd" call "%~dp0local.cmd"
if "%SDK_SRC%"=="" (echo Set SDK_SRC in tools\local.cmd ^(the ReXGlue source tree^) & exit /b 1)
if "%LLVM%"=="" set LLVM=C:\llvm\bin
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=%LLVM%;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
cd /d "%SDK_SRC%"
cmake --preset win-amd64 -B out\build\tests -DCMAKE_C_COMPILER="%LLVM%\clang.exe" -DCMAKE_CXX_COMPILER="%LLVM%\clang++.exe" "-DCMAKE_C_FLAGS=-march=x86-64-v3" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3" -DREXGLUE_BUILD_TESTS=ON "-DREX_OUTPUT_DIR=%SDK_SRC%\out\tests-bin" >nul || exit /b 1
cmake --build out\build\tests --config Release --target unit_tests --parallel || exit /b 1
for /r out\tests-bin %%f in (unit_tests.exe) do if exist "%%f" set UT=%%f
"%UT%" %~1
