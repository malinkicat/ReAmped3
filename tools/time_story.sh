#!/bin/bash
# Seconds from creating a rider to the first help card, then from starting the first
# run (X) to the first position-triggered hint, with no other input; used to check
# game speed at different frame rates: tools/time_story.sh [game args...]
cd "$(dirname "$0")/.." && . tools/env.sh
if tasklist.exe | grep -qi amped3; then echo "a game is already running; not starting"; exit 1; fi
rmdir "$(amped3_user_dir)"/cache/Cache0/Temp 2>/dev/null
rm -f run.log
powershell.exe -NoProfile -Command "Start-Process -FilePath '$WINROOT\tools\run.cmd' -WindowStyle Hidden -ArgumentList '--scripted_input=true --headless=true --headless_keyboard_text=Rider --fps_log=true $*'" </dev/null >/dev/null 2>&1
sleep 40
tools/pad.sh start "wait 5000" start "wait 5000" a
start=$(date +%s); sleep 10.4   # the a press lands after the 10.4 s of queued steps
start=$((start + 10))
while sleep 2; do
  now=$(date +%s)
  tasklist.exe | grep -qi amped3 || { echo "exited"; exit 1; }
  if tools/shot.sh story_probe >/dev/null 2>&1 && python3 tools/is_card.py private_work/story_probe.png; then
    echo "card after $((now - start)) s"
    break
  fi
  [ $((now - start)) -gt 420 ] && { echo "timeout"; exit 1; }
done
sleep 3; tools/pad.sh x "wait 1500" x
start=$(date +%s)
if [ -n "$SNAP_PREFIX" ]; then  # fixed-time snapshots after the run starts
  for t in 6 12 18; do sleep 6; tools/shot.sh "${SNAP_PREFIX}_$t" >/dev/null; done
  exit 0
fi
while sleep 0.5; do
  now=$(date +%s)
  if tools/shot.sh story_probe >/dev/null 2>&1 && python3 tools/is_card.py private_work/story_probe.png --hint; then
    echo "first hint after $((now - start)) s; fps: $(grep 'fps=' run.log | tail -8 | grep -o 'fps=[0-9]*' | sort | uniq -c | sort -rn | head -2 | tr '\n' ' ')"
    exit 0
  fi
  [ $((now - start)) -gt 120 ] && { echo "no hint"; exit 1; }
done
