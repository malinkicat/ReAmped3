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
# A Steam Deck by its board (Jupiter: LCD, Galileo: OLED), docked or not: the
# launcher and game otherwise go by the 1280x800 screen.
case "$(cat /sys/devices/virtual/dmi/id/board_name 2>/dev/null)" in
  Jupiter|Galileo) export REAMPED3_STEAM_DECK=1 ;;
esac
mkdir -p "$STEAM_COMPAT_DATA_PATH"
echo "proton: $proton"

# "Music: your streaming app": the game runs under Proton and can't see Linux
# music players, so pass the playing MPRIS player (Spotify first) on once a
# second as media_session.txt ("playing" or "stopped", then "Artist - Title"),
# and carry out the game's requests in media_command.txt (next, previous,
# pause, play, volume N).
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
  while pgrep -f '[a]mped3\.exe' >/dev/null; do
    players=$(gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
      --method org.freedesktop.DBus.ListNames 2>/dev/null | grep -o "org\.mpris\.MediaPlayer2\.[A-Za-z0-9_.-]*" | sort -r)
    playing="" first=""
    for p in $(echo "$players" | grep -i spotify) $(echo "$players" | grep -vi spotify); do
      [ -n "$first" ] || first=$p
      q "$p" --method org.freedesktop.DBus.Properties.Get org.mpris.MediaPlayer2.Player PlaybackStatus | grep -q Playing && { playing=$p; break; }
    done
    # The game appends one command a line: take the file (new ones go to a
    # fresh one) and carry them out in order.
    if [ -f media_command.txt ] && mv -f media_command.txt media_command.work 2>/dev/null; then
      sleep 0.2  # a write the game had already begun lands in the taken file first
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
        pause) [ -n "$playing" ] && { q "$playing" --method org.mpris.MediaPlayer2.Player.Pause >/dev/null; paused_player=$playing; playing=""; } ;;
        play) [ -z "$playing" ] && { q "$p" --method org.mpris.MediaPlayer2.Player.Play >/dev/null; playing=$p; paused_player=""; } ;;
        volume\ *) v=${cmd#volume }
          q "$p" --method org.freedesktop.DBus.Properties.Set org.mpris.MediaPlayer2.Player Volume "<$(awk "BEGIN{print $v/100}")>" >/dev/null ;;
      esac
      done < media_command.work
      rm -f media_command.work
    fi
    if [ -n "$playing" ]; then
      m=$(q "$playing" --method org.freedesktop.DBus.Properties.Get org.mpris.MediaPlayer2.Player Metadata)
      title=$(echo "$m" | sed -n -e "s/.*'xesam:title': <'\([^']*\)'>.*/\1/p" -e "s/.*'xesam:title': <\"\([^\"]*\)\">.*/\1/p" | head -1)
      artist=$(echo "$m" | sed -n -e "s/.*'xesam:artist': <\['\([^']*\)'.*/\1/p" -e "s/.*'xesam:artist': <\[\"\([^\"]*\)\".*/\1/p" | head -1)
      printf 'playing\n%s\n' "${artist:+$artist - }$title" > .media_session.tmp
    else
      printf 'stopped\n\n' > .media_session.tmp
    fi
    mv -f .media_session.tmp media_session.txt
    sleep 1
  done
  # The game quit with the player as its music (its last request): pause it.
  if grep -qx 'pause' media_command.txt 2>/dev/null && [ -n "$playing" ]; then
    q "$playing" --method org.mpris.MediaPlayer2.Player.Pause >/dev/null
  fi
  rm -f media_session.txt media_command.txt
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
exit $status
