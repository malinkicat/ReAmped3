#!/bin/bash
# Frame-time benchmark standing at the busiest open-world view (top of
# Loveseat, Northstar: 12-15k draws a frame), with the progressed save:
#   tools/stand_bench.sh label [game args...]     (STAND_SECONDS=15 by default)
# Repeatable where the ride route isn't (AI riders and events vary run to run).
# Swaps the progressed save in and the user's back (tools/prog_save.sh), unless
# it's already in. Same metrics as tools/ride_bench.sh.
cd "$(dirname "$0")/.." && . tools/env.sh
label=$1; shift
# Single-pass rendering as players get it (the launcher's default), unless the
# arguments choose (e.g. --gpu_tile_collapse=false to measure the 3-strip path).
case " $* " in *--gpu_tile_collapse=*) ;; *) set -- "$@" --gpu_tile_collapse=true ;; esac
secs=${STAND_SECONDS:-15}
own=0
if [ ! -f private_work/.prog_save_active ]; then
  tools/prog_save.sh in >/dev/null || exit 1
  own=1; trap 'taskkill.exe /f /im amped3.exe >/dev/null 2>&1; sleep 2; tools/prog_save.sh out >/dev/null' EXIT
fi
rm -f ${AMPED3_BUILD:-build}/swaptimes.csv
# This save opens on the trail map: quick_ride stops there (no presses on it);
# up held 4 s takes the cursor to the top-most drop point, Loveseat (shorter
# holds stop near challenges around it), and A drops in there.
QUICK_STOP_SCENE=TrailMap QUICK_NO_SPEED=1 QUICK_TIMEOUT=90 tools/quick_ride.sh --swap_times=true "$@" >/dev/null
# Then the stick held back (y -1: braking) from the drop on, so the rider stays
# at the top (one script: a second pad.sh write can replace the first unread).
# The map can take a few seconds to accept input (later on a busy PC): hold
# up until the camera has swung onto Loveseat (the picture changes a lot), at
# most 3 tries; never press A elsewhere (it would open an event's card).
sleep 3
moved=0
# The map opens with the cursor resting on Big Back, whose card pops up after
# 2-3 s (sooner at 8x) and blocks the D-pad: if it's showing (a dark panel on
# the left), B closes it and up follows at once.
card() {
  python3 - "$1" <<'PY'
import sys
from PIL import Image, ImageStat
im = Image.open(sys.argv[1]).convert('L')
# The card sits further in on an ultrawide screen (the UI keeps 16:9 in the middle).
box = (330, 170, 530, 250) if im.width / im.height > 2.0 else (100, 150, 300, 230)
im = im.resize((1280, 720)).crop(box)
sys.exit(0 if ImageStat.Stat(im).mean[0] < 42 else 1)  # card ~33, map 52-72
PY
}
for try in 1 2 3; do
  tools/shot.sh stand_map0 >/dev/null 2>&1
  if card private_work/stand_map0.png; then tools/pad.sh b "wait 400" "up 4000"; else tools/pad.sh "up 4000"; fi
  sleep 5.5; tools/shot.sh stand_map1 >/dev/null 2>&1
  card private_work/stand_map1.png && continue  # a card again: try again
  python3 - private_work/stand_map0.png private_work/stand_map1.png <<'PY' && { moved=1; break; }
import sys
from PIL import Image, ImageChops, ImageStat
# Right half, upper map: an event's card (on the left) doesn't count as a move.
a, b = (Image.open(f).convert('L').resize((320, 180)).crop((160, 20, 320, 130)) for f in sys.argv[1:3])
diff = ImageStat.Stat(ImageChops.difference(a, b)).mean[0]
sys.exit(0 if diff > 15 else 1)  # moved ~40, stuck 0
PY
done
[ $moved = 1 ] || { echo "$label: the map cursor never reached Loveseat (private_work/stand_map1.png); not measured"; exit 1; }
[ -n "$STAND_ROUTE_ONLY" ] && { echo "$label: route ok (cursor on Loveseat), stopping before the drop"; exit 0; }
# STAND_PROFILE=N: also a CPU profile of N s from 3 s after the drop (build/cpu_profile.csv, tools/cpu_profile.py).
tools/pad.sh a "wait 3000" ${STAND_PROFILE:+"profile $STAND_PROFILE"} "stick 0 -1 $(( (secs + 20) * 1000 ))"; sleep 12
# (private_work/stand_view.png: what was measured.)
where=""
tools/shot.sh stand_view >/dev/null 2>&1
sleep $((secs + 2)); taskkill.exe /f /im amped3.exe >/dev/null 2>&1; sleep 1
fps=$(echo "$@" | grep -o -- '--framerate=[0-9]*' | cut -d= -f2)
python3 - "$label" "$secs" "${fps:-60}" "${AMPED3_BUILD:-build}" "$where" <<'PY'
import csv, sys, statistics as st
label, secs, fps = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
rows = [(float(a), float(b)) for a, b, *_ in list(csv.reader(open(sys.argv[4] + '/swaptimes.csv')))[1:]]
end = rows[-1][0]
ft = [f for t, f in rows if t > end - secs]
s = sorted(ft, reverse=True)
low = lambda p: 1000.0 / st.mean(s[:max(1, int(len(s) * p))])
print(f"{label:<14} avg {len(ft)/secs:6.1f} | 1% low {low(0.01):6.1f} | 0.1% low {low(0.001):6.1f} | "
      f"P99 {s[len(s)//100]:5.2f}ms | stdev {st.pstdev(ft):4.2f}ms ({len(ft)} frames){sys.argv[5]}")
PY
