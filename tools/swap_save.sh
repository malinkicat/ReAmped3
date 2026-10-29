#!/bin/bash
# Move the user's save out of the way for test runs (a fresh rider is created),
# and put it back afterwards:
#   tools/swap_save.sh out     (backs up to private_work/user_save_backup_<time>)
#   tools/swap_save.sh back    (restores the newest backup and checks it)
cd "$(dirname "$0")/.." && . tools/env.sh
S="$(amped3_user_dir)"/B13EBABEBABEBABE/545407D4
case $1 in
  out)
    tasklist.exe | grep -qi amped3 && { echo "game running"; exit 1; }
    b=private_work/user_save_backup_$(date +%Y%m%d_%H%M%S)
    cp -r "$S" "$b" && diff -rq "$S" "$b" >/dev/null || { echo "backup failed"; exit 1; }
    rm -rf "$S" && echo "save moved to $b" ;;
  back)
    tasklist.exe | grep -qi amped3 && { echo "game running"; exit 1; }
    b=$(ls -d private_work/user_save_backup_* | sort | tail -1)
    rm -rf "$S" && cp -r "$b" "$S" && diff -rq "$S" "$b" && echo "save restored from $b" ;;
esac
