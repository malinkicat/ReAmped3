#!/bin/bash
# Boot to Load a Rider (no saved riders), press A on New Rider with the headless
# keyboard answering "Rider", and report whether a save was created.
#   tools/newrider_test.sh label [game args...]
cd "$(dirname "$0")/.." && . tools/env.sh
label=$1; shift
taskkill.exe /f /im amped3.exe >/dev/null 2>&1; sleep 1; rm -f run.log
powershell.exe -NoProfile -Command "Start-Process -FilePath '$WINROOT\tools\run.cmd' -WindowStyle Hidden -ArgumentList '--scripted_input=true --headless=true --headless_keyboard_text=Rider --log_level=debug $*'" </dev/null >/dev/null 2>&1
for i in $(seq 1 9); do sleep 2; tools/pad.sh a; done
sleep 4; tools/pad.sh a; sleep 10
tools/shot.sh "nr_$label" >/dev/null
echo "$label: XamContentCreate calls: $(grep -c 'XamContentCreate[^E]' run.log)  saves: $(ls "$(amped3_user_dir)"/B13EBABEBABEBABE/545407D4/00000001 | wc -l)"
