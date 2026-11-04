#!/bin/bash
# ReAmped 3 on the Steam Deck and Linux: starts amped3_launcher.exe under
# Proton (Steam's own copy; add this file to Steam as a non-Steam game).
# Proton 10.0 is preferred (it ran this game about 50% faster than the 11-based
# versions); PROTON_NAME=<folder in steamapps/common> picks another.
cd "$(dirname "$0")"
here=$(pwd)
# Steam's folder: ~/.steam/steam (the Deck, most installs) or
# ~/.local/share/Steam (some distributions, without the ~/.steam link).
steam=$HOME/.steam/steam
[ -d "$steam/steamapps/common" ] || steam=$HOME/.local/share/Steam
common=$steam/steamapps/common
proton="$common/${PROTON_NAME:-Proton 10.0}/proton"
[ -x "$proton" ] || proton=$(ls -d "$common"/Proton\ [0-9]*/proton 2>/dev/null | sort -V | tail -1)
[ -x "$proton" ] || { echo "No Proton found in $common: install Proton 10.0 from Steam (Library > Tools)."; exit 1; }
export STEAM_COMPAT_CLIENT_INSTALL_PATH=$steam
export STEAM_COMPAT_DATA_PATH=$here/pfx
# Gaming Mode (gamescope): present through Xwayland, not gamescope's own
# Vulkan layer. With the layer, about 40 frames a minute finished a whole
# refresh late (60 FPS on the Deck OLED: 1% low ~30 FPS, against 58.8 FPS
# without it, the same as Desktop Mode).
export ENABLE_GAMESCOPE_WSI=0
# More swap chain images for the vsync presents (the launcher asks for them
# under Proton): vkd3d-proton then rarely waits for an image Xwayland /
# gamescope still holds.
export VKD3D_SWAPCHAIN_IMAGES=5
# No xalia (Proton's controller helper for games without pad support: the
# launcher and game have their own); its window starting a few seconds in
# could take gamescope's focus.
export PROTON_USE_XALIA=0
# A Steam Deck by its board (Jupiter: LCD, Galileo: OLED), docked or not: the
# launcher and game otherwise go by the 1280x800 screen.
case "$(cat /sys/devices/virtual/dmi/id/board_name 2>/dev/null)" in
  Jupiter|Galileo) export REAMPED3_STEAM_DECK=1 ;;
esac
mkdir -p "$STEAM_COMPAT_DATA_PATH"
echo "proton: $proton"

# "Music: your streaming app": the game runs under Proton and can't see Linux
# music players, so pass the playing MPRIS player (Spotify first) on about once
# a second as media_session.txt ("playing" or "stopped", then "Artist - Title"),
# and carry out the game's requests in media_command.txt (next, previous,
# "next hold" / "previous hold" = skip but stay paused, pause, play, volume N)
# as soon as they come (checked every 0.1 s).
# While the game runs it takes every core: each program started here is slow
# to start then (a look at the players that started a dozen took 2.5 s, and
# skips waited for it), so this uses bash's own matching and waits, looks for
# new players and checks the game is still there every few seconds, and
# carries out a request before anything else.
wait_game() {
  sleep 5
  until pgrep -f '[a]mped3\.exe' >/dev/null; do
    pgrep -f '[a]mped3_launcher\.exe' >/dev/null || return 1
    sleep 1
  done
}
(
  command -v gdbus >/dev/null || exit 0
  wait_game || exit 0
  q() { gdbus call --session --dest "$1" --object-path /org/mpris/MediaPlayer2 "${@:2}" 2>/dev/null; }
  exec {tick}<> <(:)                  # never has data: read -t on it waits
  nap() { read -t "$1" -u "$tick"; }  # without starting a program
  players="" playing="" first="" paused_player="" reported="" scanned=-99 alive=$SECONDS
  # Spotify's window keeps drawing behind the game: its renderer process took
  # 60-80% of a core while it played (25% paused) and cost the game about 17
  # FPS of 1% low on the Deck. Frozen while the game runs (its music and media
  # controls are the main process's and carry on), thawed when the game goes,
  # however it goes (and at the next start, after a crash of this script).
  frozen=""
  freeze() {
    local r
    for r in $(pgrep -f 'spotify --type=renderer'); do
      [[ " $frozen " == *" $r "* ]] || { kill -STOP "$r" 2>/dev/null && frozen+=" $r"; }
    done
  }
  thaw() { [ -n "$frozen" ] && kill -CONT $frozen 2>/dev/null; frozen=""; }
  trap thaw EXIT
  trap exit TERM INT HUP
  pkill -CONT -f 'spotify --type=renderer'
  find_players() {
    local names p
    names=$(gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
      --method org.freedesktop.DBus.ListNames 2>/dev/null)
    players=""
    for p in $names; do  # Spotify first
      p=${p//[\'\[\](),]/}
      case "$p" in org.mpris.MediaPlayer2.*spotify*) players="$p $players" ;; org.mpris.MediaPlayer2.*) players+=" $p" ;; esac
    done
    scanned=$SECONDS
    freeze  # (Spotify started meanwhile, or a new renderer)
  }
  # The playing one (Spotify first), and the first one open.
  look() {
    local p
    playing="" first=""
    for p in $players; do
      [ -n "$first" ] || first=$p
      [[ $(q "$p" --method org.freedesktop.DBus.Properties.Get org.mpris.MediaPlayer2.Player PlaybackStatus) == *Playing* ]] &&
        { playing=$p; break; }
    done
  }
  # The game appends one request a line: take the file (new ones go to a
  # fresh one) and carry them out in order.
  requests() {
    [ -f media_command.txt ] && mv -f media_command.txt media_command.work 2>/dev/null || return 1
    nap 0.05  # a write the game had already begun lands in the taken file first
    local cmd p v
    while IFS= read -r cmd || [ -n "$cmd" ]; do
      cmd=${cmd%$'\r'}
      # Nothing playing: the player we paused, else the first one open
      # (Spotify first), e.g. Spotify just opened and not started yet.
      p=${playing:-${paused_player:-$first}}
      [ -n "$p" ] && case "$cmd" in
        next|previous)
          q "$p" --method org.mpris.MediaPlayer2.Player.$([ "$cmd" = next ] && echo Next || echo Previous) >/dev/null
          # A skip with it stopped (not paused for the game's music): play too.
          [ -z "$playing" ] && [ -z "$paused_player" ] && { q "$p" --method org.mpris.MediaPlayer2.Player.Play >/dev/null; playing=$p; } ;;
        next\ hold|previous\ hold)
          # The game has the music (a video, a scripted song): Spotify plays
          # on a skip, so pause it again straight after.
          q "$p" --method org.mpris.MediaPlayer2.Player.$([ "$cmd" = "next hold" ] && echo Next || echo Previous) >/dev/null
          q "$p" --method org.mpris.MediaPlayer2.Player.Pause >/dev/null; paused_player=${paused_player:-$p}; playing="" ;;
        # (The player paused first stays the one to resume: another one paused
        # meanwhile doesn't take its place.)
        pause) [ -n "$playing" ] && { q "$playing" --method org.mpris.MediaPlayer2.Player.Pause >/dev/null
          [ -z "$paused_player" ] && paused_player=$playing; playing=""; } ;;
        play|play\ start) [ -z "$playing" ] && {
          # "play start": the game starting it for a ride, from the start of the song.
          [ "$cmd" = "play start" ] && q "$p" --method org.mpris.MediaPlayer2.Player.Seek -- -9000000000 >/dev/null
          q "$p" --method org.mpris.MediaPlayer2.Player.Play >/dev/null; playing=$p; paused_player=""; } ;;
        volume\ *) v=${cmd#volume }
          [[ $v =~ ^[0-9]+$ ]] && q "$p" --method org.freedesktop.DBus.Properties.Set org.mpris.MediaPlayer2.Player Volume \
            "<$(( v / 100 )).$(printf '%02d' $(( v % 100 )))>" >/dev/null ;;
      esac
    done < media_command.work  # (the next one takes its place)
  }
  report() {
    local m title="" artist="" now
    now=$'stopped\n'
    if [ -n "$playing" ]; then
      m=$(q "$playing" --method org.freedesktop.DBus.Properties.Get org.mpris.MediaPlayer2.Player Metadata)
      local t1="'xesam:title': <'([^']*)'>" t2="'xesam:title': <\"([^\"]*)\">"
      local a1="'xesam:artist': <\['([^']*)'" a2="'xesam:artist': <\[\"([^\"]*)\""
      [[ $m =~ $t1 || $m =~ $t2 ]] && title=${BASH_REMATCH[1]}
      [[ $m =~ $a1 || $m =~ $a2 ]] && artist=${BASH_REMATCH[1]}
      now="playing"$'\n'"${artist:+$artist - }$title"
    fi
    # Written only when it changes (the game reads it twice a second).
    [ "$now" = "$reported" ] && return
    printf '%s\n' "$now" > .media_session.tmp && mv -f .media_session.tmp media_session.txt && reported=$now
  }
  # The game has started: a player playing is paused (with the streaming app
  # as the game's music, the game starts it again for a ride).
  find_players
  look
  [ -n "$playing" ] && { q "$playing" --method org.mpris.MediaPlayer2.Player.Pause >/dev/null; playing=""; }
  while :; do
    # A request first, with what the last look found (the player has the
    # new track's name in under 0.1 s).
    requests && { nap 0.1; look; report; }
    if (( SECONDS - alive >= 2 )); then
      pgrep -f '[a]mped3\.exe' >/dev/null || break
      alive=$SECONDS
    fi
    (( SECONDS - scanned >= 5 )) || [ -z "$players" ] && find_players
    look
    report
    for _ in 1 2 3 4 5 6 7 8 9 10; do
      [ -f media_command.txt ] && break
      nap 0.1
    done
  done
  # The game has gone (quit, closed or killed): its last requests, and the
  # player is left paused however it ended (paused at the start and at the end).
  look
  requests
  look
  [ -n "$playing" ] && q "$playing" --method org.mpris.MediaPlayer2.Player.Pause >/dev/null
  rm -f media_session.txt media_command.txt media_command.work
) >/dev/null 2>&1 &
bridge=$!

# Controller input even when the window isn't focused (gamescope/KWin don't
# always give a Proton window focus).
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
# KDE desktop (Steam Deck Desktop Mode): keep the screen on while it runs.
inhibit=()
pgrep -u "$(id -u)" -x plasmashell >/dev/null && command -v kde-inhibit >/dev/null &&
  inhibit=(kde-inhibit --power --screenSaver)
# Not exec: the bridge's last step (pausing the player when the game quits)
# runs after the game has gone, and Steam ends what's left when this exits.
"${inhibit[@]}" "$proton" run "$here/amped3_launcher.exe" --log_file=amped3.log "$@"
status=$?
wait "$bridge"
pkill -CONT -f 'spotify --type=renderer'  # (the bridge thaws it; in case it couldn't)
exit $status
