#!/bin/bash
# tools/soak.sh MINUTES [game args...]: progressed save (prog_save in/out), random riding,
# pause and challenge inputs; memory and GPU memory every ~30 s to private_work/soak_mem.csv;
# re-drops from the trail map (map check on a 16:9 crop). Kill the script, not the
# game, to stop early, then run tools/prog_save.sh out if the save is still swapped.
cd "$(dirname "$0")/.." && . tools/env.sh
S=private_work
mins=${1:-60}; shift; EXTRA="$*"
tools/prog_save.sh in || exit 1
trap 'taskkill.exe /f /im amped3.exe >/dev/null 2>&1; sleep 2; tools/prog_save.sh out' EXIT
QUICK_STOP_SCENE=TrailMap QUICK_TIMEOUT=90 tools/quick_ride.sh --user_music=on --gpu_tile_collapse=true --fps_log=true --hang_watch=10 $EXTRA >/dev/null 2>&1
# Never soak at the story's 8x guest time.
g=$(grep -o 'guest time x[0-9.]*' run.log | tail -1)
[ -z "$g" ] || [ "$g" = "guest time x1" ] || { echo "soak: still at ${g#guest time } after quick_ride; not soaking"; exit 1; }
tools/pad.sh b; sleep 2; tools/pad.sh "up 2000"; sleep 3.5; tools/pad.sh a; sleep 3; tools/pad.sh a; sleep 12
echo "t,ws_mb,private_mb,gpu_mb" > $S/soak_mem.csv
end=$(( $(date +%s) + mins*60 )); n=0
while [ $(date +%s) -lt $end ]; do
  tasklist.exe | grep -qi "^amped3.exe" || { echo "GAME EXITED at $(date +%T)"; break; }
  n=$((n+1))
  case $((RANDOM % 8)) in
    0|1|2) tools/pad.sh "stick 0 -1 4000" ;;
    3) tools/pad.sh "stick -0.8 -0.6 2500" ;;
    4) tools/pad.sh "stick 0.8 -0.6 2500" ;;
    5) tools/pad.sh "a 400" "stick 0 -1 2000" ;;
    6) tools/pad.sh "x" "stick 0 -1 2000" ;;
    7) tools/pad.sh start "wait 1500" b "wait 800" ;;
  esac
  sleep 5
  if [ $((n % 6)) = 0 ]; then
    m=$(powershell.exe -NoProfile -Command "\$p=Get-Process amped3 -ErrorAction SilentlyContinue; if(\$p){'{0},{1}' -f [int](\$p.WorkingSet64/1MB),[int](\$p.PrivateMemorySize64/1MB)}" | tr -d '\r')
    echo "$(date +%T),$m,$(nvidia-smi.exe --query-gpu=memory.used --format=csv,noheader,nounits | tr -d "\r ")" >> $S/soak_mem.csv
    # Back on the map (a run ended): drop in again at the cursor's lift.
    tools/shot.sh soak_check >/dev/null 2>&1
    python3 -c "
from PIL import Image
im=Image.open('private_work/soak_check.png'); w,h=im.size; fw=min(w,h*16//9); fh=fw*9//16
im.crop(((w-fw)//2,(h-fh)//2,(w+fw)//2,(h+fh)//2)).resize((1280,720)).save('private_work/soak_check169.png')"
    if python3 tools/is_screen.py private_work/soak_check169.png map 2>/dev/null; then
      tools/pad.sh a; sleep 3; tools/pad.sh a; sleep 10
    fi
  fi
done
echo "soak done at $(date +%T), $n steps"
