@echo off
rem Play test: fullscreen, 3x internal resolution (4K from 720p), 60 FPS at the real
rem 60 Hz vblank. Pass --framerate=120 / 144 / 0 for higher (GPU emulation tops out near 100).
rem Frame rate is logged to run.log every two seconds.
call "%~dp0run.cmd" --fullscreen=true --draw_resolution_scale_x=3 --draw_resolution_scale_y=3 --resolution_scale=3 --framerate=60 --fps_log=true %*
