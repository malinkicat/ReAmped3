#!/bin/bash
# Unlimited-FPS gameplay benchmark on the first run (same route each time):
#   tools/bench.sh label [game args...]   -> mean fps over ~16 s of riding
cd "$(dirname "$0")/.." && . tools/env.sh
label=$1; shift
SNAP_PREFIX="bench_$label" tools/time_story.sh --framerate=0 --vsync=false --fps_log=true "$@" >/dev/null
sleep 4
grep "fps=" run.log | tail -10 | grep -o "fps=[0-9.]*" | cut -d= -f2 |
  awk -v l="$label" '{s+=$1; if (min==""||$1<min) min=$1} END {printf "%-14s mean=%.1f min=%.1f (n=%d)\n", l, s/NR, min, NR}'
