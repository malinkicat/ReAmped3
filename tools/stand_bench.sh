#!/bin/bash
# Frame-time benchmark standing at the busiest open-world view (top of
# Loveseat, Northstar: 12-15k draws a frame), with the progressed save:
#   tools/stand_bench.sh label [game args...]     (STAND_SECONDS=15 by default)
# Repeatable where the ride route isn't (AI riders and events vary run to run).
# Swaps the progressed save in and the user's back (tools/prog_save.sh), unless
# it's already in. Same metrics as tools/ride_bench.sh.
cd "$(dirname "$0")/.." && . tools/env.sh
label=$1; shift
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
QUICK_STOP_SCENE=TrailMap QUICK_TIMEOUT=90 tools/quick_ride.sh --swap_times=true "$@" >/dev/null
# Then the stick held back (y -1: braking) from the drop on, so the rider stays
# at the top (one script: a second pad.sh write can replace the first unread).
sleep 3; tools/pad.sh "up 4000"; sleep 5.5; tools/pad.sh a "wait 3000" "stick 0 -1 $(( (secs + 20) * 1000 ))"; sleep 12
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
