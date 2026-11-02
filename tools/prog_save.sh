#!/bin/bash
# Swap in the progressed test save (story past the intro, rider in a hoodie and
# baggy trousers, events unlocked; opens on the Northstar trail map) and put
# the user's save back afterwards:
#   tools/prog_save.sh in     (user's save -> private_work/user_save_backup_<time>)
#   tools/prog_save.sh out    (restores exactly that backup, checks it)
# PROG_SAVE=dir picks another saved copy (default private_work/user_save_backup_20261028_130635).
cd "$(dirname "$0")/.." && . tools/env.sh
S="$(amped3_user_dir)"/B13EBABEBABEBABE/545407D4
P=${PROG_SAVE:-private_work/user_save_backup_20261028_130635}
M=private_work/.prog_save_active
# (A game just killed can take a few seconds to go.)
for i in $(seq 1 15); do tasklist.exe | grep -qi "^amped3.exe" || break; sleep 1; done
tasklist.exe | grep -qi "^amped3.exe" && { echo "prog_save: game running"; exit 1; }
case $1 in
  in)
    [ -f $M ] && { echo "prog_save: already in (user's save in $(cat $M))"; exit 1; }
    [ -d "$P" ] || { echo "prog_save: no $P"; exit 1; }
    b=private_work/user_save_backup_$(date +%Y%m%d_%H%M%S)
    cp -r "$S" "$b" && diff -rq "$S" "$b" >/dev/null || { echo "prog_save: backup failed"; exit 1; }
    echo "$b" > $M
    rm -rf "$S" && cp -r "$P" "$S" && echo "prog_save: progressed save in; user's save in $b" ;;
  out)
    [ -f $M ] || { echo "prog_save: not in"; exit 1; }
    b=$(cat $M)
    rm -rf "$S" && cp -r "$b" "$S" && diff -rq "$S" "$b" && rm -f $M && echo "prog_save: user's save restored from $b" ;;
  *) echo "usage: tools/prog_save.sh in|out"; exit 1 ;;
esac
