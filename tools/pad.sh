#!/bin/bash
# Queue scripted pad input for a game started with --scripted_input=true:
#   tools/pad.sh start "wait 2000" a down a
# The game deletes the file after reading it; on drvfs a rename racing that
# delete can fail, so retry briefly.
cd "$(dirname "$0")/.." && . tools/env.sh
for try in 1 2 3 4 5; do
  printf '%s\n' "$@" > build/input_script.tmp && mv -f build/input_script.tmp build/input_script.txt 2>/dev/null && exit 0
  sleep 0.2
done
echo "pad.sh: could not write the input script" >&2; exit 1
