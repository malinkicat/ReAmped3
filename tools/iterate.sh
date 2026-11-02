#!/bin/bash
# Build, launch, wait, report: tools/iterate.sh [seconds] [extra game args...]
cd "$(dirname "$0")/.." && . tools/env.sh
if tasklist.exe | grep -qi amped3; then echo "a game is already running; not starting"; exit 1; fi
cmd.exe /c "tools\\build.cmd" 12 > build.log 2>&1 || { grep -E -A10 "FAILED:|error:" build.log | head -40; exit 1; }
rm -f run.log 2>/dev/null
powershell.exe -NoProfile -Command "Start-Process -FilePath '$WINROOT\tools\run.cmd' -WindowStyle Hidden ${2:+-ArgumentList '${*:2}'}" </dev/null >/dev/null 2>&1
sleep "${1:-25}"
if tasklist.exe | grep -qi amped3; then echo "STATUS: still running"; else echo "STATUS: exited"; fi
grep -E "critical|FATAL|error" run.log | head -10
echo "--- tail"; tail -8 run.log
