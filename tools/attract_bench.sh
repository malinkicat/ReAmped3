#!/bin/bash
# Main-thread cost A/B on this PC: boot with no input and let the title's
# attract demo (a recorded run, the same every time) play, logging the game
# main thread's CPU cycles per frame every 300 frames (--main_cycles_log).
# Caveat: the main thread also spin-waits on the GPU ring, so the cycles
# include waiting; useful only where the game thread is the limit.
#   tools/attract_bench.sh label [seconds (90)] [game args...]
# Never starts while a game is running; closes only the game it started.
cd "$(dirname "$0")/.." && . tools/env.sh
label=$1; secs=${2:-90}; shift 2 2>/dev/null
if tasklist.exe | grep -qi amped3; then echo "$label: a game is already running; not starting"; exit 1; fi
rm -f run.log
powershell.exe -NoProfile -Command "Start-Process -FilePath '$WINROOT\tools\run.cmd' -WindowStyle Hidden -ArgumentList '--scripted_input=true --headless=true --main_cycles_log=true --framerate=60 $*'" </dev/null >/dev/null 2>&1
sleep "$secs"
taskkill.exe /f /im amped3.exe >/dev/null 2>&1; sleep 2
echo "$label: $(grep -a 'Mcycles/frame' run.log | grep -o '[0-9.]* Mcycles' | cut -d' ' -f1 | tr '\n' ' ')"
