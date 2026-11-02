#!/bin/bash
# Boot straight into riding: guest time runs 8x (scripted "speed 8") through the
# unskippable story (from when it loads; at boot it hangs the intro movies), A loads the saved rider (or creates
# "Rider"), and at the help card time returns to 1x and X starts the run.
#   tools/quick_ride.sh [game args...]
# Never press Start on Load a Rider: there it deletes the rider.
# QUICK_STOP_SCENE=TrailMap: stop (success) as soon as the game pushes that
# scene instead of waiting for a ride (a save that opens on the trail map; its
# A presses would otherwise pick things on the map).
cd "$(dirname "$0")/.." && . tools/env.sh
# Never close a game someone else started (the user may be playing).
if tasklist.exe | grep -qi "^amped3\.exe"; then echo "quick_ride: a game is already running; not starting"; exit 1; fi
rmdir "$(amped3_user_dir)"/cache/Cache0/Temp 2>/dev/null
rm -f run.log
WSLENV=AMPED3_BUILD powershell.exe -NoProfile -Command "Start-Process -FilePath '$WINROOT\tools\run.cmd' -WindowStyle Hidden -ArgumentList '--scripted_input=true --headless=true --headless_keyboard_text=Rider ${QUICK_STOP_SCENE:+--scene_log=true }$*'" </dev/null >/dev/null 2>&1
# With --tall_squeeze (16:10) the game's 16:9 picture keeps its shape in the
# middle rows of an 800-row window.
card_args=""
case " $* " in *--tall_squeeze=*) card_args="--yoff 40" ;; esac
start=$(date +%s)
sped=0
while sleep 1.5; do
  # The script file is only read while the game polls input, so resend the
  # speed-up with each press until the log confirms it.
  speed=""
  # Sped up once the story loads (after the title and Load a Rider: at 8x
  # the title can stop taking presses), or after QUICK_SPEED_AT s at most.
  # QUICK_NO_SPEED=1: never (a save that opens on the trail map has no story to
  # wait through; at 8x its map shows a spot's card at once, blocking the D-pad).
  if [ $sped = 0 ] && [ -z "$QUICK_NO_SPEED" ] && { grep -qi "groverintro" run.log || [ $(( $(date +%s) - start )) -ge ${QUICK_SPEED_AT:-60} ]; }; then
    grep -q "guest time x${QUICK_SPEED:-8}" run.log && sped=1 || speed="speed ${QUICK_SPEED:-8}"
  fi
  tasklist.exe | grep -qi amped3 || { echo "quick_ride: exited"; exit 1; }
  if [ -n "$QUICK_STOP_SCENE" ] && grep -aq "scene push $QUICK_STOP_SCENE" run.log; then
    # Back to 1x if it was sped up (this save loads the story's files too).
    if grep -q "guest time x" run.log; then
      for try in $(seq 1 20); do
        tools/pad.sh "speed 1"; sleep 0.5
        [ "$(grep -o 'guest time x[0-9.]*' run.log | tail -1)" = "guest time x1" ] && break
      done
    fi
    echo "at $QUICK_STOP_SCENE after $(( $(date +%s) - start )) s ($(grep -o 'guest time x[0-9.]*' run.log | tail -1))"; exit 0
  fi
  if tools/shot.sh story_probe >/dev/null 2>&1 && python3 tools/is_card.py private_work/story_probe.png $card_args 2>/dev/null; then
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
  if [ $(( $(date +%s) - start )) -gt ${QUICK_TIMEOUT:-150} ]; then
    # Back to 1x, confirmed (one unconfirmed press can be lost: a 2026-11-02
    # soak then ran 45 minutes at 8x).
    for try in $(seq 1 20); do
      grep -q "guest time x" run.log || break
      [ "$(grep -o 'guest time x[0-9.]*' run.log | tail -1)" = "guest time x1" ] && break
      tools/pad.sh "speed 1"; sleep 0.5
    done
    echo "quick_ride: timeout ($(grep -o 'guest time x[0-9.]*' run.log | tail -1))"; exit 1
  fi
done
