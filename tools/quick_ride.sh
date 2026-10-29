#!/bin/bash
# Boot straight into riding: guest time runs 8x (scripted "speed 8") through the
# unskippable story (from 12 s, after the logos; at boot it hangs the intro movies), A loads the saved rider (or creates
# "Rider"), and at the help card time returns to 1x and X starts the run.
#   tools/quick_ride.sh [game args...]
# Never press Start on Load a Rider: there it deletes the rider.
cd "$(dirname "$0")/.." && . tools/env.sh
taskkill.exe /f /im amped3.exe >/dev/null 2>&1; sleep 1
rmdir "$(amped3_user_dir)"/cache/Cache0/Temp 2>/dev/null
rm -f run.log
powershell.exe -NoProfile -Command "Start-Process -FilePath '$WINROOT\tools\run.cmd' -WindowStyle Hidden -ArgumentList '--scripted_input=true --headless=true --headless_keyboard_text=Rider $*'" </dev/null >/dev/null 2>&1
start=$(date +%s)
sped=0
while sleep 1.5; do
  # The script file is only read while the game polls input, so resend the
  # speed-up with each press until the log confirms it.
  speed=""
  if [ $sped = 0 ] && [ $(( $(date +%s) - start )) -ge ${QUICK_SPEED_AT:-12} ]; then
    grep -q "guest time x${QUICK_SPEED:-8}" run.log && sped=1 || speed="speed ${QUICK_SPEED:-8}"
  fi
  tasklist.exe | grep -qi amped3 || { echo "quick_ride: exited"; exit 1; }
  if tools/shot.sh story_probe >/dev/null 2>&1 && python3 tools/is_card.py private_work/story_probe.png 2>/dev/null; then
    # Back to 1x before anything else; resend until the log confirms it (a second
    # pad.sh write could replace the script before the game read it).
    for try in $(seq 1 20); do
      tools/pad.sh "speed 1"; sleep 0.5
      [ "$(grep -o 'guest time x[0-9.]*' run.log | tail -1)" = "guest time x1" ] && break
    done
    sleep ${QUICK_CARD_DELAY:-0}; tools/pad.sh x "wait 1500" x
    echo "riding after $(( $(date +%s) - start )) s ($(grep -o 'guest time x[0-9.]*' run.log | tail -1))"
    exit 0
  fi
  tools/pad.sh ${speed:+"$speed"} a
  [ $(( $(date +%s) - start )) -gt 150 ] && { echo "quick_ride: timeout"; tools/pad.sh "speed 1"; exit 1; }
done
