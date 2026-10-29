#!/bin/bash
# Repeatable CPU/GPU benchmark: ride for a few seconds, pause (the frozen scene is
# still drawn every frame - the same work each frame and each run), then measure
# 15 s of presents. Use for A/B comparisons; ride_bench.sh varies too much with
# what happens in the run.
#   tools/pause_bench.sh label [game args...]   (needs a fresh rider: swap_save.sh out)
cd "$(dirname "$0")/.." && . tools/env.sh
label=$1; shift
secs=${BENCH_SECONDS:-15}
rm -f build/swaptimes.csv
tools/quick_ride.sh --swap_times=true "$@" >/dev/null || { echo "$label: no ride"; exit 1; }
sleep ${PAUSE_AFTER:-6}
tools/pad.sh start
sleep $((secs + 3))
taskkill.exe /f /im amped3.exe >/dev/null 2>&1; sleep 1
python3 - "$label" "$secs" <<'PY'
import csv, sys, statistics as st
label, secs = sys.argv[1], float(sys.argv[2])
rows = [(float(a), float(b)) for a, b in list(csv.reader(open('build/swaptimes.csv')))[1:]]
end = rows[-1][0]
ft = [f for t, f in rows if end - secs < t]
s = sorted(ft, reverse=True)
low = lambda p: 1000.0 / st.mean(s[:max(1, int(len(s) * p))])
print(f"{label:<16} avg {len(ft)/secs:6.1f} | median {1000/st.median(ft):6.1f} | 1% low {low(0.01):6.1f} | "
      f"stdev {st.pstdev(ft):4.2f}ms ({len(ft)} frames)")
PY
