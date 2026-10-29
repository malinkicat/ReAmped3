#!/bin/bash
# Repeatable benchmark ride on the Steam Deck (Desktop Mode, awake): the PC's
# quick_ride route (fresh rider, story at 8x, first run), then frame-time
# stats over the last BENCH_SECONDS (40) of riding, from swaptimes.csv.
#   tools/deck_bench.sh label [game args...]     (e.g. --framerate=0)
# PROTON_NAME=... picks another installed Proton (see tools/deck.sh);
# BENCH_PROFILE=1 also records a 10 s CPU profile during the ride.
# The ReAmped 3 save on the Deck is set aside first (plus a dated copy in
# ~/ReAmped3/save_backups) and put back afterwards: `tools/deck_bench.sh
# restore` does only that, e.g. after an interrupted bench.
cd "$(dirname "$0")/.." && . tools/env.sh
label=$1; shift
secs=${BENCH_SECONDS:-40}
KEY=$HOME/.ssh/id_ed25519_deck
SSH=(ssh -i "$KEY" -o ConnectTimeout=5 "$DECK_HOST")
SAVES='ReAmped3/pfx/pfx/drive_c/users/steamuser/Documents/amped3'
SAVE=B13EBABEBABEBABE
S=private_work/deck
mkdir -p $S

restore_save() {
  # The stash holds the player's save; whatever is in its place is the bench's.
  "${SSH[@]}" "cd $SAVES && if [ -d ~/ReAmped3/save_stash/$SAVE ]; then rm -rf $SAVE && mv ~/ReAmped3/save_stash/$SAVE . && rmdir ~/ReAmped3/save_stash && echo 'save restored'; fi"
}
pad() {
  "${SSH[@]}" "printf '%s\n' $(printf "'%s' " "$@") > ReAmped3/input_script.tmp && mv -f ReAmped3/input_script.tmp ReAmped3/input_script.txt"
}
shot() {
  "${SSH[@]}" 'p=$(pgrep -u deck -x plasmashell | head -1); for v in $(tr "\0" "\n" < /proc/$p/environ | grep -E "^(DISPLAY|XAUTHORITY|WAYLAND_DISPLAY|DBUS_SESSION_BUS_ADDRESS|XDG_RUNTIME_DIR)="); do export "$v"; done; timeout 10 spectacle -b -n -f -o $HOME/ReAmped3/bench_shot.png >/dev/null 2>&1' &&
    scp -q -i "$KEY" "$DECK_HOST:ReAmped3/bench_shot.png" $S/bench_shot.png
}
log_has() { "${SSH[@]}" "grep -q '$1' ReAmped3/amped3.log 2>/dev/null"; }

if [ "$label" = restore ]; then restore_save; exit; fi
[ -n "$label" ] || { sed -n '2,9p' "$0"; exit 1; }

tools/deck.sh stop >/dev/null
"${SSH[@]}" "pgrep -x gamescope >/dev/null" && { echo "$label: Deck is in Gaming Mode (bench needs Desktop Mode)"; exit 1; }
# Set the player's save aside (once: an existing stash means an earlier bench
# was interrupted and what's in place now is a bench save).
"${SSH[@]}" "cd $SAVES && if [ -d $SAVE ] && [ ! -d ~/ReAmped3/save_stash ]; then mkdir -p ~/ReAmped3/save_backups ~/ReAmped3/save_stash && cp -a $SAVE ~/ReAmped3/save_backups/$SAVE-\$(date +%Y%m%d-%H%M%S) && mv $SAVE ~/ReAmped3/save_stash/; else rm -rf $SAVE; fi" || exit 1
trap restore_save EXIT
"${SSH[@]}" "rm -f ReAmped3/swaptimes.csv ReAmped3/input_script.txt"
tools/deck.sh run --scripted_input=true --headless=true --headless_keyboard_text=Rider --swap_times=true "$@" >/dev/null || exit 1

# Time from the game's first log line (Proton itself takes a while to start).
for i in $(seq 1 60); do "${SSH[@]}" "test -s ReAmped3/amped3.log" && break; sleep 2; done
start=$(date +%s); sped=0; riding=0
while sleep 1.5; do
  now=$(( $(date +%s) - start ))
  tools/deck.sh status | grep -q "not running" && { echo "$label: game exited"; exit 1; }
  speed=""
  if [ $sped = 0 ] && [ $now -ge 14 ]; then
    log_has "guest time x8" && sped=1 || speed="speed 8"
  fi
  if [ $now -ge 20 ] && shot && python3 tools/is_card.py $S/bench_shot.png; then
    for try in $(seq 1 20); do
      pad "speed 1"; sleep 1
      [ "$("${SSH[@]}" "grep -o 'guest time x[0-9.]*' ReAmped3/amped3.log | tail -1")" = "guest time x1" ] && break
    done
    pad x "wait 1500" x
    riding=1; break
  fi
  pad ${speed:+"$speed"} a
  [ $now -gt 300 ] && { echo "$label: timeout"; pad "speed 1"; break; }
done
[ $riding = 1 ] || { tools/deck.sh stop >/dev/null; exit 1; }
# Linux per-thread CPU time over 10 s of riding (Wine's own threads included).
( sleep 12; "${SSH[@]}" 'pid=$(pgrep -f "^Z:.*amped3\.exe" | head -1); snap() { for t in /proc/$pid/task/*; do echo "$(cat $t/comm 2>/dev/null)|$(sed "s/.*) //" $t/stat 2>/dev/null | awk "{print \$12+\$13}")|${t##*/}"; done; }; snap > ReAmped3/.rt_a; sleep 10; snap > ReAmped3/.rt_b; awk -F"|" "NR==FNR{a[\$3]=\$2; next} {d=\$2-a[\$3]; if (d>5) printf \"%5.1f%% %s\n\", d/10, \$1}" ReAmped3/.rt_a ReAmped3/.rt_b | sort -rn; rm -f ReAmped3/.rt_a ReAmped3/.rt_b' > "$S/threads_$label.txt" 2>&1 ) &
if [ -n "$BENCH_PROFILE" ]; then
  # BENCH_PROFILE=1: a 10 s CPU profile from 10 s into the ride -> $S/cpu_profile_<label>.csv
  sleep 10; pad "profile 10"; sleep $((secs - 6))
  scp -q -i "$KEY" "$DECK_HOST:ReAmped3/cpu_profile.csv" "$S/cpu_profile_$label.csv"
else
  sleep $((secs + 4))
fi
tools/deck.sh stop >/dev/null; sleep 2
scp -q -i "$KEY" "$DECK_HOST:ReAmped3/swaptimes.csv" $S/swaptimes.csv || exit 1
scp -q -i "$KEY" "$DECK_HOST:ReAmped3/clocks.csv" $S/clocks_bench.csv 2>/dev/null
fps=$(echo "$@" | grep -o -- '--framerate=[0-9]*' | cut -d= -f2)
python3 - "$label" "$secs" "${fps:-60}" $S/swaptimes.csv $S/clocks_bench.csv <<'PY'
import csv, sys, statistics as st
label, secs, fps, path, clocks = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5]
rows = [(float(a), float(b)) for a, b in list(csv.reader(open(path)))[1:]]
end = rows[-1][0]
ft = [f for t, f in rows if t > end - secs]
s = sorted(ft, reverse=True)
low = lambda p: 1000.0 / st.mean(s[:max(1, int(len(s) * p))])
target = 1000.0 / fps if fps else st.median(ft)
hitches = sum(f > 1.5 * target for f in ft)
extra = ''
try:
    c = [r for r in csv.DictReader(open(clocks))][-int(secs):]
    extra = (f" | GPU {st.mean(int(r['gpu_mhz']) for r in c):4.0f} MHz CPU {st.mean(int(r['cpu_mhz']) for r in c):4.0f} MHz "
             f"{st.mean(float(r['apu_w']) for r in c):4.1f} W")
except Exception:
    pass
print(f"{label:<16} avg {len(ft)/secs:6.1f} | 1% low {low(0.01):6.1f} | P99 {s[len(s)//100]:5.2f}ms | "
      f"stdev {st.pstdev(ft):4.2f}ms | hitches {hitches} ({len(ft)} frames){extra}")
PY
