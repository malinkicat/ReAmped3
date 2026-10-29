#!/bin/bash
# Steam Deck test loop over SSH. Needs DECK_HOST in tools/local.env (e.g.
# DECK_HOST=deck@192.168.1.50) and, on the Deck: a password (passwd) and
# sshd on (sudo systemctl enable --now sshd).
#
#   tools/deck.sh setup          one-time: install this PC's SSH key on the Deck
#   tools/deck.sh deploy         build package -> ~/ReAmped3 (game files only the first time)
#   tools/deck.sh run [args...]  start amped3.exe under Proton (Desktop Mode), extra game args passed on
#   tools/deck.sh flags [args...]  extra game flags for every launch (Steam shortcut too); none = clear
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

# The Deck-side launcher: Proton (Experimental if installed, else the newest),
# its own prefix in ~/ReAmped3/pfx, drawing on the desktop session's display.
remote_run_script() {
  cat <<'EOF'
#!/bin/bash
cd "$(dirname "$0")"
here=$(pwd)
common=$HOME/.steam/steam/steamapps/common
proton="$common/${PROTON_NAME:-Proton - Experimental}/proton"
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
    case "$var" in DISPLAY=*|XAUTHORITY=*|WAYLAND_DISPLAY=*|XDG_RUNTIME_DIR=*) export "$var" ;; esac
  done < "/proc/$session/environ"
fi
echo "proton: $proton"
# Read-only once-a-second record of clocks, load and power while the game
# runs (clocks.csv): GPU MHz, GPU busy %, APU power W, mean CPU MHz, temp C.
(
  gpu=$(grep -l amdgpu /sys/class/hwmon/hwmon*/name | head -1 | xargs dirname)
  echo "time,gpu_mhz,gpu_busy,apu_w,cpu_mhz,temp_c"
  sleep 5
  while pgrep -f '[a]mped3\.exe' >/dev/null; do
    cpu=$(awk '{s+=$1; n++} END{printf "%d", s/n/1000}' /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq)
    echo "$(date +%T),$(( $(cat $gpu/freq1_input) / 1000000 )),$(cat /sys/class/drm/card0/device/gpu_busy_percent),$(awk '{printf "%.1f", $1/1000000}' $gpu/power1_average),$cpu,$(( $(cat $gpu/temp1_input) / 1000 ))"
    sleep 1
  done
) > clocks.csv 2>/dev/null &
# Pad input even when the window isn't focused (gamescope/KWin don't always
# give a Proton window focus; SDL drops pad events without it).
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
# PROTON_NAME=... (a folder in steamapps/common) picks another installed Proton;
# tools/deck.sh run also passes DECK_ENV="VAR=value ..." through to the game.
# Debug flags set by tools/deck.sh flags, also applied when Steam starts this.
extra=()
[ -f extra_args.txt ] && read -r -a extra < extra_args.txt
exec "$proton" run "$here/amped3.exe" --game_data_root="Z:${here//\//\\}\\game" \
  --resolution_scale=1 --draw_resolution_scale_x=1 --draw_resolution_scale_y=1 \
  --window_width=1280 --window_height=800 --fullscreen=true --framerate=60 --mnk_mode=true \
  --log_file=amped3.log --log_flush_interval=1 "${extra[@]}" "$@"
EOF
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
    remote_run_script | "${SSH[@]}" "cat > $DECK_DIR/run.sh && chmod +x $DECK_DIR/run.sh"
    echo "deployed to $DECK_HOST:~/$DECK_DIR"
    ;;
  run)
    shift
    # As a transient user service: processes started straight from an SSH
    # session are killed when it ends. Gone again when the game exits.
    "${SSH[@]}" "cd $DECK_DIR && rm -f amped3.log && systemctl --user stop reamped3 2>/dev/null; systemctl --user reset-failed reamped3 2>/dev/null; \
      systemd-run --user --quiet --collect --unit=reamped3 --working-directory=\$HOME/$DECK_DIR \
        ${PROTON_NAME:+--setenv=PROTON_NAME='$PROTON_NAME'} $(for e in $DECK_ENV; do printf -- "--setenv=%s " "$e"; done) \
        bash -c './run.sh $* > proton.log 2>&1' && sleep 2 && head -3 proton.log"
    ;;
  flags)
    # Extra game flags for every launch, including from the Steam shortcut.
    shift
    "${SSH[@]}" "echo '$*' > $DECK_DIR/extra_args.txt && cat $DECK_DIR/extra_args.txt"
    ;;
  stop)
    "${SSH[@]}" "systemctl --user stop reamped3 2>/dev/null; pkill -f '[a]mped3\.exe'; true"
    ;;
  status)
    "${SSH[@]}" "pgrep -fa '[a]mped3\.exe' | grep -v proton | cut -c1-80 || echo 'not running'"
    ;;
  logs)
    mkdir -p private_work/deck
    "${SSH[@]}" "cd $DECK_DIR && tar cf - \$(ls amped3.log proton.log *.csv settings.ini 2>/dev/null)" |
      tar xf - -C private_work/deck/
    ls -la private_work/deck/
    ;;
  ssh)
    shift
    "${SSH[@]}" "$@"
    ;;
  *)
    sed -n "2,14p" "$0"
    ;;
esac
