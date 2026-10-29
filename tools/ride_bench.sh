#!/bin/bash
# Frame-time benchmark over the last 40 s of riding on the first run (quick_ride
# route), from host swap times (when frames are presented):
#   tools/ride_bench.sh label [game args...]       (BENCH_SECONDS=40 by default)
# Metrics, as in FrameView / CapFrameX practice: average FPS; 1% and 0.1% lows
# (average FPS of the slowest 1% / 0.1% of frames); P99 frame time; frame-time
# standard deviation; mean frame-to-frame change (microstutter); hitches
# (frames longer than 1.5x the target, or 1.5x the median when unlimited).
cd "$(dirname "$0")/.." && . tools/env.sh
label=$1; shift
secs=${BENCH_SECONDS:-40}
rm -f build/swaptimes.csv
tools/quick_ride.sh --swap_times=true "$@" >/dev/null || { echo "$label: no ride"; exit 1; }
sleep $((secs + 2)); taskkill.exe /f /im amped3.exe >/dev/null 2>&1; sleep 1
fps=$(echo "$@" | grep -o -- '--framerate=[0-9]*' | cut -d= -f2)
python3 - "$label" "$secs" "${fps:-60}" <<'PY'
import csv, sys, statistics as st
label, secs, fps = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
rows = [(float(a), float(b)) for a, b in list(csv.reader(open('build/swaptimes.csv')))[1:]]
end = rows[-1][0]
ft = [f for t, f in rows if t > end - secs]
s = sorted(ft, reverse=True)
low = lambda p: 1000.0 / st.mean(s[:max(1, int(len(s) * p))])
target = 1000.0 / fps if fps else st.median(ft)
delta = st.mean(abs(b - a) for a, b in zip(ft, ft[1:]))
hitches = sum(f > 1.5 * target for f in ft)
print(f"{label:<14} avg {len(ft)/secs:6.1f} | 1% low {low(0.01):6.1f} | 0.1% low {low(0.001):6.1f} | "
      f"P99 {s[len(s)//100]:5.2f}ms | stdev {st.pstdev(ft):4.2f}ms | f2f {delta:4.2f}ms | hitches {hitches} ({len(ft)} frames)")
PY
