#!/bin/bash
# Rebuild, then play from boot into the story with the scripted pad, taking
# screenshots: tools/playtest.sh [label] [extra presses...]
cd "$(dirname "$0")/.." && . tools/env.sh
label=${1:-pt}; shift
rmdir "$(amped3_user_dir)"/cache/Cache0/Temp 2>/dev/null
tools/iterate.sh 40 --scripted_input=true --headless=true --headless_keyboard_text=Rider --hang_watch=30 --fps_log=true --hitch_profile=true $PLAYTEST_ARGS \
  | grep -E "error:|FAILED" && exit 1
alive() { tasklist.exe | grep -qi amped3; }
tools/pad.sh start "wait 5000" start "wait 5000" a          # title -> Load a Rider -> New Rider
sleep 60; alive && tools/shot.sh "${label}_0"
for i in 1 2 3 4 5 6; do
  alive || break
  tools/pad.sh start "wait 1500" a "$@"
  sleep 15; alive && tools/shot.sh "${label}_$i"
done
alive && echo "RESULT: running" || echo "RESULT: exited"
grep -E "critical|FATAL" run.log | head -3
