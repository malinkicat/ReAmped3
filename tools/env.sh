# Sourced by the tools after they cd to the repository root: paths for this
# checkout and machine. Machine-specific settings go in tools/local.env (not
# committed; see tools/local.env.example).
ROOT=$(pwd)
WINROOT=$(wslpath -w "$ROOT")
[ -f tools/local.env ] && . tools/local.env
# The game's user data folder (saves, shader cache): Documents\amped3.
amped3_user_dir() {
  if [ -z "$AMPED3_USER_DIR" ]; then
    AMPED3_USER_DIR="$(wslpath "$(cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r')")/Documents/amped3"
  fi
  echo "$AMPED3_USER_DIR"
}
