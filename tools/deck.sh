#!/bin/bash
# Steam Deck test loop over SSH. Needs DECK_HOST in tools/local.env (e.g.
# DECK_HOST=deck@192.168.1.50) and, on the Deck: a password (passwd) and
# sshd on (sudo systemctl enable --now sshd).
#
#   tools/deck.sh setup          one-time: install this PC's SSH key on the Deck
#   tools/deck.sh deploy         build package -> ~/ReAmped3 (game files only the first time)
#   tools/deck.sh run [args...]  start amped3.exe directly under Proton (Desktop Mode; run_game.sh,
#                                no launcher), extra game args passed on
#   tools/deck.sh flags [args...]  extra game flags for every launch (Steam shortcut too); none = clear
#   tools/deck.sh pad CMD...     scripted pad input (game started with --scripted_input=true)
#   tools/deck.sh shot [NAME]    screenshot of the Deck screen -> private_work/deck/NAME.png
#   tools/deck.sh stop           stop the game
#   tools/deck.sh status         is it running?
#   tools/deck.sh logs           fetch amped3.log and the .csv files into private_work/deck/
#   tools/deck.sh ssh [cmd]      shell (or one command) on the Deck
cd "$(dirname "$0")/.." && . tools/env.sh
[ -n "$DECK_HOST" ] || { echo "Set DECK_HOST in tools/local.env (e.g. DECK_HOST=deck@192.168.1.50)"; exit 1; }
DECK_DIR=${DECK_DIR:-ReAmped3}          # relative to the Deck user's home
KEY=$HOME/.ssh/id_ed25519_deck
SSH=(ssh -i "$KEY" -o ConnectTimeout=5 "$DECK_HOST")
RSYNC_SSH="ssh -i $KEY"

game_dir() {
  grep -o 'AMPED3_GAME_DIR=.*' tools/local.cmd | head -1 | cut -d= -f2- | tr -d '\r'
}

# The Deck-side start scripts, both written on deploy:
#   run.sh       what the Steam shortcut starts: amped3_launcher.exe, which
#                starts the game with the player's settings
#   run_game.sh  amped3.exe directly with fixed settings (tools/deck.sh run and
#                the benchmarks, so scripted runs never stop at the launcher)
# Both: Proton 10.0 by default (about 50% faster here than the Proton
# 11-based Experimental and 11.0; PROTON_NAME overrides), its own prefix in
# ~/ReAmped3/pfx, drawing on the logged-in session's display.
remote_run_script() {
  cat <<'EOF'
#!/bin/bash
cd "$(dirname "$0")"
here=$(pwd)
common=$HOME/.steam/steam/steamapps/common
proton="$common/${PROTON_NAME:-Proton 10.0}/proton"
[ -x "$proton" ] || proton=$(ls -d "$common"/Proton\ [0-9]*/proton 2>/dev/null | sort -V | tail -1)
[ -x "$proton" ] || { echo "no Proton found in $common (install one from Steam)"; exit 1; }
export STEAM_COMPAT_CLIENT_INSTALL_PATH=$HOME/.steam/steam
export STEAM_COMPAT_DATA_PATH=$here/pfx
mkdir -p "$STEAM_COMPAT_DATA_PATH"
# Run in the logged-in session (Desktop or Gaming Mode): take its display
# variables from plasmashell, else Steam itself.
session=$(pgrep -u "$(id -u)" -x plasmashell | head -1)
[ -n "$session" ] || session=$(pgrep -u "$(id -u)" -x steam | head -1)
if [ -n "$session" ]; then
  while IFS= read -r -d '' var; do
    case "$var" in DISPLAY=*|XAUTHORITY=*|WAYLAND_DISPLAY=*|XDG_RUNTIME_DIR=*|DBUS_SESSION_BUS_ADDRESS=*) export "$var" ;; esac
  done < "/proc/$session/environ"
fi
echo "proton: $proton"
# Keep the previous session's recordings (a quick second launch must not wipe
# the one before).
for f in threads.log clocks.csv; do [ -s "$f" ] && mv -f "$f" "$f.prev"; done
# The recorders below start with the game: with the launcher first, wait while
# it is open (give up if it closes without starting the game).
wait_game() {
  sleep 5
  until pgrep -f '[a]mped3\.exe' >/dev/null; do
    pgrep -f '[a]mped3_launcher\.exe' >/dev/null || return 1
    sleep 1
  done
}
# Read-only once-a-second record of clocks, load and power while the game
# runs (clocks.csv): GPU MHz, GPU busy %, APU power W, mean CPU MHz, temp C.
(
  gpu=$(grep -l amdgpu /sys/class/hwmon/hwmon*/name | head -1 | xargs dirname)
  echo "time,gpu_mhz,gpu_busy,apu_w,cpu_mhz,temp_c"
  wait_game || exit 0
  while pgrep -f '[a]mped3\.exe' >/dev/null; do
    cpu=$(awk '{s+=$1; n++} END{printf "%d", s/n/1000}' /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq)
    echo "$(date +%T),$(( $(cat $gpu/freq1_input) / 1000000 )),$(cat /sys/class/drm/card0/device/gpu_busy_percent),$(awk '{printf "%.1f", $1/1000000}' $gpu/power1_average),$cpu,$(( $(cat $gpu/temp1_input) / 1000 ))"
    sleep 1
  done
) > clocks.csv 2>/dev/null &
# Per-thread CPU time every 10 s while the game runs (threads.log): % of one
# core per thread over each window, busiest first. Read-only (/proc).
(
  wait_game || exit 0
  snap() { for t in /proc/$1/task/*; do echo "$(cat $t/comm 2>/dev/null)|$(sed "s/.*) //" $t/stat 2>/dev/null | awk '{print $12+$13}')|${t##*/}"; done; }
  while pid=$(pgrep -f '^Z:.*amped3\.exe' | head -1) && [ -n "$pid" ]; do
    snap $pid > .threads_a; sleep 10; snap $pid > .threads_b
    echo "== $(date +%T)"
    awk -F'|' 'NR==FNR{a[$3]=$2; next} {d=$2-a[$3]; if (d>20) printf "%5.1f%% %s\n", d/10, $1}' .threads_a .threads_b | sort -rn | head -8
  done
  rm -f .threads_a .threads_b
) > threads.log 2>/dev/null &
# Pad input even when the window isn't focused (gamescope/KWin don't always
# give a Proton window focus; SDL drops pad events without it).
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
# PROTON_NAME=... (a folder in steamapps/common) picks another installed Proton;
# tools/deck.sh run also passes DECK_ENV="VAR=value ..." through to the game.
# Debug flags set by tools/deck.sh flags, also applied when Steam starts this.
extra=()
[ -f extra_args.txt ] && read -r -a extra < extra_args.txt
# In Desktop Mode, keep the screen on while the game runs (Gaming Mode does
# this itself); the inhibition ends with the game.
inhibit=()
pgrep -u "$(id -u)" -x plasmashell >/dev/null && command -v kde-inhibit >/dev/null &&
  inhibit=(kde-inhibit --power --screenSaver)
EOF
  if [ "$1" = launcher ]; then
    # The launcher passes its arguments on to the game (as play_diag.bat's on
    # Windows) and, under Proton, stays until the game exits.
    cat <<'EOF'
exec "${inhibit[@]}" "$proton" run "$here/amped3_launcher.exe" \
  --log_file=amped3.log --log_flush_interval=1 "${extra[@]}" "$@"
EOF
  else
    cat <<'EOF'
exec "${inhibit[@]}" "$proton" run "$here/amped3.exe" --game_data_root="Z:${here//\//\\}\\game" \
  --resolution_scale=1 --draw_resolution_scale_x=1 --draw_resolution_scale_y=1 \
  --window_width=1280 --window_height=800 --fullscreen=true --framerate=60 --mnk_mode=true \
  --log_file=amped3.log --log_flush_interval=1 "${extra[@]}" "$@"
EOF
  fi
}

case "$1" in
  setup)
    [ -f "$KEY" ] || ssh-keygen -t ed25519 -N "" -f "$KEY" -C reamped3-deck >/dev/null
    ssh-copy-id -i "$KEY.pub" "$DECK_HOST" && "${SSH[@]}" 'echo "connected to $(hostname)"'
    ;;
  deploy)
    pkg=dist/Amped3-recomp-test
    powershell.exe -NoProfile -File "$WINROOT\\tools\\package.ps1" >/dev/null || exit 1
    "${SSH[@]}" "mkdir -p $DECK_DIR/game" || exit 1
    if ! "${SSH[@]}" "test -f $DECK_DIR/game/default.xex"; then
      src=$(wslpath "$(game_dir)")
      echo "first deploy: copying the game files ($(du -sh "$src" | cut -f1)), this takes a while"
      rsync -a --info=progress2 -e "$RSYNC_SSH" "$src/" "$DECK_HOST:$DECK_DIR/game/" || exit 1
    fi
    rsync -a --checksum --itemize-changes -e "$RSYNC_SSH" "$pkg/" "$DECK_HOST:$DECK_DIR/" || exit 1
    remote_run_script launcher | "${SSH[@]}" "cat > $DECK_DIR/run.sh && chmod +x $DECK_DIR/run.sh" || exit 1
    remote_run_script game | "${SSH[@]}" "cat > $DECK_DIR/run_game.sh && chmod +x $DECK_DIR/run_game.sh" || exit 1
    echo "deployed to $DECK_HOST:~/$DECK_DIR"
    ;;
  run)
    shift
    # As a transient user service: processes started straight from an SSH
    # session are killed when it ends. Gone again when the game exits.
    # Never start a second game (e.g. one the player launched from Steam, or
    # its launcher still open), and keep the previous run's logs as *.prev.
    if "${SSH[@]}" "systemctl --user is-active -q reamped3 || ! pgrep -f '[a]mped3(_launcher)?\.exe' >/dev/null"; then :; else
      echo "a ReAmped 3 game is already running on the Deck (not ours); not starting"; exit 1; fi
    "${SSH[@]}" "cd $DECK_DIR && for f in amped3.log threads.log clocks.csv; do [ -f \$f ] && mv -f \$f \$f.prev; done; systemctl --user stop reamped3 2>/dev/null; systemctl --user reset-failed reamped3 2>/dev/null; \
      systemd-run --user --quiet --collect --unit=reamped3 --working-directory=\$HOME/$DECK_DIR \
        ${PROTON_NAME:+--setenv=PROTON_NAME='$PROTON_NAME'} $(for e in $DECK_ENV; do printf -- "--setenv=%s " "$e"; done) \
        bash -c './run_game.sh $* > proton.log 2>&1' && sleep 2 && head -3 proton.log"
    ;;
  flags)
    # Extra game flags for every launch, including from the Steam shortcut.
    shift
    "${SSH[@]}" "echo '$*' > $DECK_DIR/extra_args.txt && cat $DECK_DIR/extra_args.txt"
    ;;
  stop)
    # Only our own runs (the transient service), never a game the player started.
    "${SSH[@]}" "systemctl --user stop reamped3 2>/dev/null; true"
    ;;
  status)
    "${SSH[@]}" "pgrep -fa '[a]mped3\.exe' | grep -v proton | cut -c1-80 || echo 'not running'"
    ;;
  pad)
    # Scripted pad input for a game started with --scripted_input=true:
    #   tools/deck.sh pad a "wait 1000" "stick 0 1 2000"
    shift
    "${SSH[@]}" "printf '%s\n' $(printf "'%s' " "$@") > $DECK_DIR/input_script.tmp && mv -f $DECK_DIR/input_script.tmp $DECK_DIR/input_script.txt"
    ;;
  shot)
    # Screenshot of the Deck's screen (Desktop Mode) -> private_work/deck/<name>.png
    "${SSH[@]}" 'p=$(pgrep -u deck -x plasmashell | head -1); for v in $(tr "\0" "\n" < /proc/$p/environ | grep -E "^(DISPLAY|XAUTHORITY|WAYLAND_DISPLAY|DBUS_SESSION_BUS_ADDRESS|XDG_RUNTIME_DIR)="); do export "$v"; done; timeout 10 spectacle -b -n -f -o $HOME/'"$DECK_DIR"'/shot.png >/dev/null 2>&1' &&
      mkdir -p private_work/deck && scp -q -i "$KEY" "$DECK_HOST:$DECK_DIR/shot.png" "private_work/deck/${2:-shot}.png" && echo "private_work/deck/${2:-shot}.png"
    ;;
  logs)
    mkdir -p private_work/deck
    "${SSH[@]}" "cd $DECK_DIR && tar cf - \$(ls amped3.log proton.log threads.log *.csv settings.ini 2>/dev/null)" |
      tar xf - -C private_work/deck/
    ls -la private_work/deck/
    ;;
  ssh)
    shift
    "${SSH[@]}" "$@"
    ;;
  *)
    sed -n "2,17p" "$0"
    ;;
esac
