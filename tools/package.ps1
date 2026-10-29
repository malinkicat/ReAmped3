# Test build folder + zip for another PC: the launcher, amped3.exe, the SDK
# DLLs, the Visual C++ runtime DLLs (app-local) and a README - no game files
# (the player points the launcher at their own extracted Amped 3 folder).
# Build first (tools\build.cmd, tools\build_launcher.cmd).
#   powershell -File tools\package.ps1 [-Out dist] [-Name Amped3-recomp-test] [-SdkDir build] [-Drop]
# -Drop also copies the zip to $env:AMPED3_DROP_DIR (e.g. a folder another
# test PC fetches from).
# -SdkDir: where rexruntime.dll / rexgpu-xenos.dll come from (build\ has the
# last SDK built, e.g. the trial tree; project\sdk-bin the committed one).
param([string]$Out = "dist", [string]$Name = "Amped3-recomp-test", [string]$SdkDir = "build",
      [switch]$Drop)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dir = Join-Path (Join-Path $root $Out) $Name
$files = @(
    "build\amped3.exe",
    "build\amped3_launcher.exe",
    "$SdkDir\rexruntime.dll",
    "$SdkDir\rexgpu-xenos.dll",
    "build\amd_fidelityfx_dx12.dll"
)
foreach ($f in $files) {
    if (-not (Test-Path (Join-Path $root $f))) { throw "$f missing: build first" }
}
$crt = Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" |
    Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw "Visual C++ redistributable folder not found" }

Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $dir | Out-Null
foreach ($f in $files) { Copy-Item (Join-Path $root $f) $dir }
foreach ($dll in @("msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
    Copy-Item (Join-Path $crt.FullName $dll) $dir
}
@(
    "Amped 3 recomp - test build ($(Get-Date -Format yyyy-MM-dd))",
    "",
    "Needs: Windows 10/11 x64, a CPU with AVX2, a DirectX 12 GPU, and your own",
    "extracted copy of Amped 3 (the folder with default.xex). No game files are",
    "included.",
    "",
    "1. Copy your extracted Amped 3 folder to this PC (anywhere).",
    "2. Run amped3_launcher.exe, click Browse... and pick default.xex in it.",
    "3. Choose resolution / FPS / options and click Launch.",
    "",
    "In game: F10 shows the frame-time graph. The window title shows the settings.",
    "Saves: Documents\amped3. Settings: settings.ini here.",
    "If something goes wrong, send amped3.log from this folder.",
    "",
    "play_diag.bat: the launcher with frame-time logging on (frametimes.csv,",
    "swaptimes.csv and per-second FPS lines in amped3.log)."
) | Set-Content -Encoding ascii (Join-Path $dir "README.txt")
@(
    '@echo off',
    'rem The launcher with frame-time logging: frametimes.csv (game frames),',
    'rem swaptimes.csv (presents) and per-second FPS lines in amped3.log.',
    'start "" "%~dp0amped3_launcher.exe" --frame_times=true --swap_times=true --fps_log=true --gpu_pass_profile=true --gpu_pass_profile_passes=false'
) | Set-Content -Encoding ascii (Join-Path $dir "play_diag.bat")
@(
    "Amped 3 test checklist",
    "",
    "Use play_diag.bat for each run. For each setting, ride for 30+ seconds,",
    "then quit and send back amped3.log, settings.ini and the .csv files before the next run",
    "(each run replaces them).",
    "",
    "1. 4K 3x, 60 FPS: smooth? any hitches (F10 graph)?",
    "2. 4K 3x, 120 FPS (tearing on, G-Sync): average / lows on the F10 graph.",
    "3. 4K 3x, unlimited.",
    "4. Look for rendering glitches: seams down the screen (at about 1/3 and 2/3",
    "   of the width), wrong shadows, flicker, missing blur/glow, menus.",
    "5. Music: one track at a time in menus, loading and riding."
) | Set-Content -Encoding ascii (Join-Path $dir "TEST-CHECKLIST.txt")

$zip = Join-Path (Join-Path $root $Out) "$Name.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $dir -DestinationPath $zip
"$dir"
"$zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
if ($Drop) {
    $dropDir = $env:AMPED3_DROP_DIR
    if (-not $dropDir) { throw "Set AMPED3_DROP_DIR for -Drop" }
    New-Item -ItemType Directory -Force -Path $dropDir | Out-Null
    Copy-Item $zip $dropDir
    "dropped in $dropDir"
}
