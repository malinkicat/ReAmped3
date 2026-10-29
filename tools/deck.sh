#!/bin/bash
# Steam Deck test loop over SSH. Needs DECK_HOST in tools/local.env (e.g.
# DECK_HOST=deck@192.168.1.50) and, on the Deck: a password (passwd) and
# sshd on (sudo systemctl enable --now sshd).
#
#   tools/deck.sh setup          one-time: install this PC's SSH key on the Deck
#   tools/deck.sh deploy         build package -> ~/ReAmped3 (game files only the first time)
#   tools/deck.sh run [args...]  start amped3.exe under Proton (Desktop Mode), extra game args passed on
#   tools/deck.sh stop           stop the game
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
proton="$common/Proton - Experimental/proton"
[ -x "$proton" ] || proton=$(ls -d "$common"/Proton\ [0-9]*/proton 2>/dev/null | sort -V | tail -1)
[ -x "$proton" ] || { echo "no Proton found in $common (install one from Steam)"; exit 1; }
export STEAM_COMPAT_CLIENT_INSTALL_PATH=$HOME/.steam/steam
export STEAM_COMPAT_DATA_PATH=$here/pfx
mkdir -p "$STEAM_COMPAT_DATA_PATH"
export DISPLAY=${DISPLAY:-:0}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
echo "proton: $proton"
exec "$proton" run "$here/amped3.exe" --game_data_root="Z:${here//\//\\}\\game" \
  --fullscreen=true --log_file=amped3.log --log_flush_interval=1 "$@"
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
    "${SSH[@]}" "cd $DECK_DIR && rm -f amped3.log && (setsid ./run.sh $* > proton.log 2>&1 &) && sleep 1 && head -3 proton.log"
    ;;
  stop)
    "${SSH[@]}" "pkill -f amped3.exe; true"
    ;;
  logs)
    mkdir -p private_work/deck
    rsync -a -e "$RSYNC_SSH" "$DECK_HOST:$DECK_DIR/{amped3.log,proton.log,*.csv,settings.ini}" private_work/deck/ 2>/dev/null
    ls -la private_work/deck/
    ;;
  ssh)
    shift
    "${SSH[@]}" "$@"
    ;;
  *)
    sed -n '2,12p' "$0"
    ;;
esac
