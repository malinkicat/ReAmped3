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
# BENCH_LAUNCHER=1: start through run.sh and the launcher with the player's
# settings.ini (as the Steam shortcut does), not run_game.sh's fixed flags.
# BENCH_GAMING=1: the same in Gaming Mode, started by Steam itself (Gaming Mode
# shows only what Steam starts): the test flags go in through extra_args.txt
# (read by run.sh) and screenshots come from gamescopectl.
gaming=${BENCH_GAMING:-}
launcher=${BENCH_LAUNCHER:-}; [ -n "$gaming" ] && launcher=1
# The Deck's ReAmped 3 shortcut (run.sh) as a Steam link, DECK_STEAM_GAME in
# tools/local.env (steam://rungameid/<shortcut id << 32 | 0x02000000>).
STEAM_GAME=$DECK_STEAM_GAME
[ -n "$gaming" ] && [ -z "$STEAM_GAME" ] && { echo "Set DECK_STEAM_GAME in tools/local.env"; exit 1; }
shot() {
  if [ -n "$gaming" ]; then
    "${SSH[@]}" 'export GAMESCOPE_WAYLAND_DISPLAY=gamescope-0; f=$HOME/ReAmped3/bench_shot.png; rm -f $f; timeout 5 gamescopectl screenshot $f >/dev/null 2>&1; for i in $(seq 15); do [ -s $f ] && break; sleep 0.2; done; sleep 0.3; [ -s $f ]' &&
      scp -q -i "$KEY" "$DECK_HOST:ReAmped3/bench_shot.png" $S/bench_shot.png
    return
  fi
  "${SSH[@]}" 'p=$(pgrep -u deck -x plasmashell | head -1); for v in $(tr "\0" "\n" < /proc/$p/environ | grep -E "^(DISPLAY|XAUTHORITY|WAYLAND_DISPLAY|DBUS_SESSION_BUS_ADDRESS|XDG_RUNTIME_DIR)="); do export "$v"; done; timeout 10 spectacle -b -n -f -o $HOME/ReAmped3/bench_shot.png >/dev/null 2>&1' &&
    scp -q -i "$KEY" "$DECK_HOST:ReAmped3/bench_shot.png" $S/bench_shot.png
}
log_has() { "${SSH[@]}" "grep -q '$1' ReAmped3/amped3.log 2>/dev/null"; }

if [ "$label" = restore ]; then tools/deck.sh stop >/dev/null; restore_save; exit; fi
[ -n "$label" ] || { sed -n '2,9p' "$0"; exit 1; }

# Never touch the player's save or start a game while they might be playing:
# only in Desktop Mode (plasmashell running), and only with no game running
# other than our own transient service (nor the launcher open). The bench
# starts the game with `tools/deck.sh run`, i.e. run_game.sh: straight into
# the game, never the launcher.
if [ -n "$gaming" ]; then
  "${SSH[@]}" "pgrep -f '^gamescope ' >/dev/null" || { echo "$label: Deck is not in Gaming Mode; not starting"; exit 1; }
else
  "${SSH[@]}" "pgrep -u deck -x plasmashell >/dev/null" || { echo "$label: Deck is not in Desktop Mode; not starting"; exit 1; }
fi
tools/deck.sh stop >/dev/null
"${SSH[@]}" "pgrep -f '[a]mped3(_launcher)?\.exe' >/dev/null" && { echo "$label: a ReAmped 3 game is running on the Deck; not starting"; exit 1; }
# Set the player's save aside (once: an existing stash means an earlier bench
# was interrupted and what's in place now is a bench save).
"${SSH[@]}" "cd $SAVES && if [ -d $SAVE ] && [ ! -d ~/ReAmped3/save_stash ]; then mkdir -p ~/ReAmped3/save_backups ~/ReAmped3/save_stash && cp -a $SAVE ~/ReAmped3/save_backups/$SAVE-\$(date +%Y%m%d-%H%M%S) && mv $SAVE ~/ReAmped3/save_stash/; else rm -rf $SAVE; fi" || exit 1
# BENCH_SAVE=name: start from a copy of ~/ReAmped3/bench_saves/<name> instead
# of a fresh rider (e.g. openworld, a copy of a save that reached the open world).
if [ -n "$BENCH_SAVE" ]; then
  "${SSH[@]}" "cp -a ~/ReAmped3/bench_saves/$BENCH_SAVE/$SAVE $SAVES/" || exit 1
fi
if [ "$label" = explore ]; then
  # Leave the game running for working out a route by hand (tools/deck.sh pad /
  # shot); `tools/deck_bench.sh restore` afterwards puts the player's save back.
  tools/deck.sh run --scripted_input=true --headless=true --headless_keyboard_text=Rider --fps_log=true "$@" >/dev/null
  echo "exploring: tools/deck.sh pad ... / tools/deck.sh shot NAME; then tools/deck_bench.sh restore"
  exit 0
fi
# Keep the screen on only while benching (the user's own idle-off setting,
# 10 minutes, protects the OLED otherwise): wake it (kscreen-doctor --dpms on)
# and a user-level kde-inhibit, ended on exit.
inhibit_on() {
  "${SSH[@]}" 'p=$(pgrep -u deck -x plasmashell | head -1); for v in $(tr "\0" "\n" < /proc/$p/environ | grep -E "^(DISPLAY|WAYLAND_DISPLAY|DBUS_SESSION_BUS_ADDRESS|XDG_RUNTIME_DIR)="); do export "$v"; done; kscreen-doctor --dpms on >/dev/null 2>&1; setsid nohup kde-inhibit --screenSaver --power sleep 1800 >/dev/null 2>&1 & echo $! > ReAmped3/bench_inhibit.pid'
}
inhibit_off() {
  "${SSH[@]}" 'p=$(cat ReAmped3/bench_inhibit.pid 2>/dev/null) && kill -- -$p 2>/dev/null; kill $p 2>/dev/null; rm -f ReAmped3/bench_inhibit.pid'
}
# The game (or, started through the launcher, the launcher that starts it) runs.
alive() {
  if [ -n "$launcher" ]; then "${SSH[@]}" "pgrep -f '[a]mped3(_launcher)?\.exe' >/dev/null"
  else ! tools/deck.sh status | grep -q "not running"; fi
}
# A launcher-started game is ended like the player's: the game, then the launcher.
stop_game() {
  if [ -n "$launcher" ]; then
    "${SSH[@]}" "pkill -f '[a]mped3\.exe'; sleep 3; pkill -f '[a]mped3_launcher\.exe'; systemctl --user stop reamped3 2>/dev/null; true"
  else
    tools/deck.sh stop >/dev/null
  fi
}
on_exit() {
  inhibit_off; restore_save
  # (The screen capture loop stays off: its screenshots stutter the game.)
  [ -n "$gaming" ] && "${SSH[@]}" "cd ReAmped3 && { [ -f extra_args.saved ] && mv -f extra_args.saved extra_args.txt || rm -f extra_args.txt; }; true"
}
[ -n "$gaming" ] || inhibit_on
trap on_exit EXIT
"${SSH[@]}" "rm -f ReAmped3/swaptimes.csv ReAmped3/input_script.txt ReAmped3/cpu_profile.csv"
flags="--ride_after 3 --scripted_input=true --headless=true --headless_keyboard_text=Rider --swap_times=true $*"
if [ -n "$gaming" ]; then
  "${SSH[@]}" "cd ReAmped3 && { [ -f amped3.log ] && mv -f amped3.log amped3.log.prev; true; } && { [ -f extra_args.txt ] && cp -f extra_args.txt extra_args.saved; true; } && echo '$flags' > extra_args.txt && systemctl --user stop reamped3-capture 2>/dev/null; timeout 20 steam $STEAM_GAME >/dev/null 2>&1; true" || exit 1
elif [ -n "$launcher" ]; then
  "${SSH[@]}" "cd ReAmped3 && { [ -f amped3.log ] && mv -f amped3.log amped3.log.prev; true; } && systemctl --user reset-failed reamped3 2>/dev/null; systemd-run --user --quiet --collect --unit=reamped3 --working-directory=\$HOME/ReAmped3 bash -c './run.sh $flags > proton.log 2>&1'" || exit 1
else
  tools/deck.sh run --scripted_input=true --headless=true --headless_keyboard_text=Rider --swap_times=true "$@" >/dev/null || exit 1
fi

# Time from the game's first log line (Proton itself takes a while to start).
for i in $(seq 1 60); do "${SSH[@]}" "test -s ReAmped3/amped3.log" && break; sleep 2; done
start=$(date +%s); sped=0; riding=0
if [ "$BENCH_ROUTE" = openworld ]; then
  # BENCH_ROUTE=openworld (with BENCH_SAVE=openworld): Northstar from Loveseat,
  # the player's busy test spot. A through the intro, title and Load a Rider
  # (the first rider) up to the rider menu, A on "Hit the Slopes" once, then on
  # the map: hold up (the cursor goes to Loveseat), A, and A again to set off.
  menu=0
  while [ $(( $(date +%s) - start )) -lt 240 ]; do
    alive || { echo "$label: game exited"; exit 1; }
    shot || continue
    if python3 tools/is_screen.py $S/bench_shot.png map; then
      # Stand at the top for OW_STAND s (10) first: the view over the park is
      # the heaviest part (12-14k draws a frame) and is inside the measurement.
      # BENCH_PROFILE_DROP=N: an N s CPU profile from the drop itself (the
      # load and the ride's first seconds), instead of the stand's.
      sleep 1; pad "up 2000"; sleep 3.5
      if [ -n "$BENCH_PROFILE_DROP" ]; then pad a "profile $BENCH_PROFILE_DROP"; else pad a; fi
      sleep 12
      # BENCH_PROFILE: profile the stand (the script is only read between
      # steps, so not during the long stick step that follows).
      [ -n "$BENCH_PROFILE" ] && pad "profile 10"
      # BENCH_MOVES=ride: riding as a player does - forward, carve left, jump
      # with a flip, carve right, jump with a flip - in a fixed order for the
      # whole ride (the same in every run), instead of straight down. Stick up
      # is forward; down stops the rider.
      if [ "$BENCH_MOVES" = ride ]; then
        moves=(a)
        for n in 1 2 3 4 5 6; do
          moves+=("stick 0 1 3000" "stick -0.8 0.6 2000" "a 700" "stick 0 1 900" "stick 0 1 1500"
                  "stick 0.8 0.6 2000" "a 700" "stick 0 1 900" "stick 0 1 1500")
        done
        ( sleep ${OW_STAND:-10}; pad "${moves[@]}" ) &
      else
        ( sleep ${OW_STAND:-10}; pad a "stick 0 1 120000" ) &
      fi
      riding=1; break
    fi
    if [ $menu = 0 ] && python3 tools/is_screen.py $S/bench_shot.png menu; then
      pad a; menu=1; continue
    fi
    [ $menu = 0 ] && pad a
    sleep 1
  done
  [ $riding = 1 ] || echo "$label: no ride (menu=$menu)"
fi
while [ "$BENCH_ROUTE" != openworld ] && sleep 1.5; do
  now=$(( $(date +%s) - start ))
  alive || { echo "$label: game exited"; exit 1; }
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
[ $riding = 1 ] || { stop_game; exit 1; }
# BENCH_PIN="cpus-for-draws|cpus-for-the-rest" (e.g. "6|0-5"): pin the game's
# GPU Draws thread and keep its other threads off those CPUs (taskset on our
# own process only), to test thread placement.
if [ -n "$BENCH_PIN" ]; then
  "${SSH[@]}" "pid=\$(pgrep -f '^Z:.*amped3\.exe' | head -1); for t in /proc/\$pid/task/*; do case \"\$(cat \$t/comm)\" in 'GPU Draws'*) taskset -pc ${BENCH_PIN%%|*} \${t##*/} ;; *) taskset -pc ${BENCH_PIN##*|} \${t##*/} ;; esac; done >/dev/null 2>&1"
fi
# BENCH_SHOTS=N: screenshots back to back for N seconds from the start of the
# ride -> $S/burst_<label>/NN.png (spectacle; about one a second).
if [ -n "$BENCH_SHOTS" ]; then
  ( mkdir -p "$S/burst_$label"; end=$(( $(date +%s) + BENCH_SHOTS )); n=0
    while [ $(date +%s) -lt $end ]; do
      n=$((n + 1)); f=$(printf '%02d' $n)
      "${SSH[@]}" "p=\$(pgrep -u deck -x plasmashell | head -1); for v in \$(tr '\\0' '\\n' < /proc/\$p/environ | grep -E '^(DISPLAY|XAUTHORITY|WAYLAND_DISPLAY|DBUS_SESSION_BUS_ADDRESS|XDG_RUNTIME_DIR)='); do export \"\$v\"; done; timeout 10 spectacle -b -n -f -o \$HOME/ReAmped3/burst_$f.png >/dev/null 2>&1"
    done
    scp -q -i "$KEY" "$DECK_HOST:ReAmped3/burst_*.png" "$S/burst_$label/" && "${SSH[@]}" "rm -f ReAmped3/burst_*.png" ) &
fi
# A screenshot 2 s into the ride (before the measured window), to check the
# picture -> $S/ride_<label>.png
( sleep 2; shot && cp $S/bench_shot.png "$S/ride_$label.png" ) &
# Linux per-thread CPU time over 10 s of riding (Wine's own threads included).
( sleep 12; "${SSH[@]}" 'pid=$(pgrep -f "^Z:.*amped3\.exe" | head -1); snap() { for t in /proc/$pid/task/*; do echo "$(cat $t/comm 2>/dev/null)|$(sed "s/.*) //" $t/stat 2>/dev/null | awk "{print \$12+\$13}")|${t##*/}"; done; }; snap > ReAmped3/.rt_a; sleep 10; snap > ReAmped3/.rt_b; awk -F"|" "NR==FNR{a[\$3]=\$2; next} {d=\$2-a[\$3]; if (d>5) printf \"%5.1f%% %s\n\", d/10, \$1}" ReAmped3/.rt_a ReAmped3/.rt_b | sort -rn; rm -f ReAmped3/.rt_a ReAmped3/.rt_b' > "$S/threads_$label.txt" 2>&1 ) &
# BENCH_RIDE_HOOK: commands run 4 s into the ride (pad / shot available), e.g.
# to look at the pause menus on the Deck; the numbers are then meaningless.
if [ -n "$BENCH_RIDE_HOOK" ]; then sleep 4; eval "$BENCH_RIDE_HOOK"; fi
if [ -n "$BENCH_PROFILE_DROP" ]; then
  sleep $((secs + 4))
  scp -q -i "$KEY" "$DECK_HOST:ReAmped3/cpu_profile.csv" "$S/cpu_profile_$label.csv" || echo "$label: no CPU profile" >&2
elif [ -n "$BENCH_PROFILE" ]; then
  # BENCH_PROFILE=1: a 10 s CPU profile from 10 s into the ride -> $S/cpu_profile_<label>.csv
  sleep 10; [ "$BENCH_ROUTE" = openworld ] || pad "profile 10"; sleep $((secs - 6))
  scp -q -i "$KEY" "$DECK_HOST:ReAmped3/cpu_profile.csv" "$S/cpu_profile_$label.csv" || echo "$label: no CPU profile" >&2
else
  sleep $((secs + 4))
fi
stop_game; sleep 2
scp -q -i "$KEY" "$DECK_HOST:ReAmped3/swaptimes.csv" $S/swaptimes.csv || exit 1
scp -q -i "$KEY" "$DECK_HOST:ReAmped3/clocks.csv" $S/clocks_bench.csv 2>/dev/null
fps=$(echo "$@" | grep -o -- '--framerate=[0-9]*' | cut -d= -f2)
python3 - "$label" "$secs" "${fps:-60}" $S/swaptimes.csv $S/clocks_bench.csv <<'PY'
import csv, sys, statistics as st
label, secs, fps, path, clocks = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5]
rows = [(float(r[0]), float(r[1])) for r in list(csv.reader(open(path)))[1:] if len(r) >= 2]  # (a last line cut short when the game stops)
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
