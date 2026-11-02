@echo off
rem Launch the recompiled Amped 3. Extra arguments are passed through.
if exist "%~dp0local.cmd" call "%~dp0local.cmd"
if "%AMPED3_GAME_DIR%"=="" (echo Set AMPED3_GAME_DIR in tools\local.cmd ^(see local.cmd.example^) & exit /b 1)
rem AMPED3_BUILD: another build folder (e.g. build_test for experimental DLLs).
if "%AMPED3_BUILD%"=="" set AMPED3_BUILD=build
cd /d "%~dp0..\%AMPED3_BUILD%"
start "" amped3.exe --game_data_root="%AMPED3_GAME_DIR%" --window_width=1280 --window_height=720 --fullscreen=false --log_file="%~dp0..\run.log" --log_flush_interval=1 --mnk_mode=true %*
