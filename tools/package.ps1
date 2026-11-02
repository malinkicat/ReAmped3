# Test build folder + zip for another PC: the launcher, amped3.exe, the SDK
# DLLs, the Visual C++ runtime DLLs (app-local) and a README - no game files
# (the player points the launcher at their own extracted Amped 3 folder).
# Build first (tools\build.cmd, tools\build_launcher.cmd).
#   powershell -File tools\package.ps1 [-Out dist] [-Name Amped3-recomp-test] [-SdkDir build] [-Drop] [-Release]
# -Release: no test checklist, diagnostics launcher or test logging; the README
# says "ReAmped 3 (date)" instead of "test build".
# -Drop also copies the zip to $env:AMPED3_DROP_DIR (e.g. a folder another
# test PC fetches from).
# -SdkDir: where rexruntime.dll / rexgpu-xenos.dll come from (build\ has the
# last SDK built, e.g. the trial tree; project\sdk-bin the committed one).
param([string]$Out = "dist", [string]$Name = "Amped3-recomp-test", [string]$SdkDir = "build",
      [switch]$Drop, [string]$DiagArgs = "", [switch]$Release)
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
# NVIDIA's DLSS runtime (DLAA) is never packaged: the launcher fetches it from
# NVIDIA when a player picks DLAA and accepts NVIDIA's license (vendor\dlss).
# Licenses of the bundled runtime, FidelityFX and the code this builds on.
Copy-Item (Join-Path $PSScriptRoot "..\THIRD_PARTY_NOTICES.md") $dir
# Exo 2 (SIL OFL) for the launcher and the in-game prompts.
Copy-Item -Recurse (Join-Path $PSScriptRoot "..\project\assets\fonts") (Join-Path $dir "fonts")
# The ReAmped 3 logo for the launcher header (generated with FLUX.1 schnell, tools/art/logo_gen.py + logo_cut.py).
Copy-Item (Join-Path $PSScriptRoot "..\project\assets\logo\logo.bmp") $dir
# The upscaler the launcher runs to make the sharp UI pack on the player's PC
# from their own game files (ui_pack.cpp): Real-ESRGAN ncnn Vulkan v0.2.5.0
# and two of its models, with their licenses. Not in git (~47 MB):
# AMPED3_ESRGAN_DIR, default private_work\esrgan (the release zip, unpacked).
$esrgan = if ($env:AMPED3_ESRGAN_DIR) { $env:AMPED3_ESRGAN_DIR } else { Join-Path $PSScriptRoot "..\private_work\esrgan" }
if (Test-Path (Join-Path $esrgan "realesrgan-ncnn-vulkan.exe")) {
    $to = Join-Path $dir "tools\esrgan"
    New-Item -ItemType Directory -Force -Path (Join-Path $to "models") | Out-Null
    Copy-Item (Join-Path $esrgan "realesrgan-ncnn-vulkan.exe"), (Join-Path $esrgan "vcomp140.dll") $to
    foreach ($m in @("realesrgan-x4plus-anime", "realesrgan-x4plus")) {
        Copy-Item (Join-Path $esrgan "models\$m.bin"), (Join-Path $esrgan "models\$m.param") (Join-Path $to "models")
    }
    Copy-Item -Recurse (Join-Path $esrgan "licenses") (Join-Path $to "licenses")
} else {
    Write-Warning "Real-ESRGAN not found at ${esrgan}: the build has no sharp UI"
}
# A ready-made sharp UI pack (tools/ui_pack.py) is made from the game's own art, so
# it's only added to private builds: AMPED3_UI_PACK = its folder of <key>.png.
if ($env:AMPED3_UI_PACK -and (Test-Path $env:AMPED3_UI_PACK)) {
    New-Item -ItemType Directory -Force -Path (Join-Path $dir "textures") | Out-Null
    Copy-Item -Recurse $env:AMPED3_UI_PACK (Join-Path $dir "textures\replace")
}
# Your own music goes here (launcher: Music).
New-Item -ItemType Directory -Force -Path (Join-Path $dir "music") | Out-Null
@(
    "Put your own music here: mp3, ogg, flac or wav files (subfolders are fine).",
    "",
    "Then pick ""Music: your music files"" in the launcher (A / Enter on the",
    "Music row chooses another folder), or in the game's Start > Options > Audio.",
    "The game's soundtrack goes quiet while yours plays; the D-pad (or F8 / F9)",
    "changes tracks."
) | Set-Content -Encoding UTF8 (Join-Path $dir "music\README.txt")
foreach ($dll in @("msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
    Copy-Item (Join-Path $crt.FullName $dll) $dir
}
@(
    $(if ($Release) { "ReAmped 3 ($(Get-Date -Format yyyy-MM-dd))" } else { "ReAmped 3 - test build ($(Get-Date -Format yyyy-MM-dd))" }),
    "",
    "Needs: Windows 10/11 x64, a CPU with AVX2, a DirectX 12 GPU, and your own",
    "extracted copy of Amped 3 (the folder with default.xex). No game files are",
    "included.",
    "",
    "1. Copy your extracted Amped 3 folder to this PC (anywhere).",
    "2. Run amped3_launcher.exe, choose Browse, open that folder and choose",
    "   Choose this folder (a controller works too: A opens, B goes up, X chooses).",
    "3. Choose resolution / FPS / options and press RIDE.",
    "",
    "Sharp UI (optional): tick it and the launcher makes the menus' and HUD's art",
    "sharp for high resolutions from your own game files, once (about 5 minutes,",
    "in the background; you can ride meanwhile). It uses Real-ESRGAN, an offline",
    "image upscaler (not an AI assistant; nothing leaves your PC). X on Sharp UI",
    "makes it again.",
    "ReAmped logo (optional): the ReAmped 3 logo on the title and loading screen.",
    "Ultrawide / 16:10: pick 'Match the display' to fill the screen.",
    "Controls: the launcher's Controls button remaps controller buttons and",
    "keyboard keys (Reset puts the defaults back).",
    "",
    "In game: F10 shows the frame-time graph. The window title shows the settings.",
    "Saves: Documents\amped3. Settings: settings.ini here.",
    "If something goes wrong, send amped3.log from this folder."
) + $(if ($Release) { @() } else { @(
    "",
    "play_diag.bat: the launcher with frame-time logging on (frametimes.csv,",
    "swaptimes.csv and per-second FPS lines in amped3.log).") }) | Set-Content -Encoding ascii (Join-Path $dir "README.txt")
if (-not $Release) {
@(
    '@echo off',
    'rem The launcher with frame-time logging: frametimes.csv (game frames),',
    'rem swaptimes.csv (presents) and per-second FPS lines in amped3.log.',
    'start "" "%~dp0amped3_launcher.exe" --frame_times=true --swap_times=true --fps_log=true --gpu_pass_profile=true --gpu_pass_profile_passes=false'
) | Set-Content -Encoding ascii (Join-Path $dir "play_diag.bat")
}
# Test builds: extra logging the launcher passes to the game (-DiagArgs).
if ($DiagArgs -and -not $Release) {
    @("# Logging for this test build (the launcher adds these to the game's options).", $DiagArgs) |
        Set-Content -Encoding ascii (Join-Path $dir "diag_args.txt")
}
if (-not $Release) {
@(
    "ReAmped 3 test checklist",
    "",
    "Start it from the launcher (or play_diag.bat). For each setting, ride for 30+",
    "seconds, then quit and send back amped3.log, settings.ini and the .csv files",
    "before the next run (each run replaces them).",
    "",
    "1. Start-up: no 2K / Indie Built movies; one loading screen (Amped 3 logo,",
    "   LOADING..., the publisher's notice); then the title. With 'ReAmped logo'",
    "   ticked: the ReAmped 3 logo on both (RE part of the word, one outline).",
    "2. Launcher: every setting explained on the left when selected; Resolution",
    "   is a list of your display's modes; 30 FPS is the default; Save; Controls",
    "   lists what each control does; the game folder says 'USA, tested'.",
    "3. Your resolution and frame rate (e.g. 5120x2160, 120): smooth? hitches",
    "   on the F10 graph? Ultrawide: wider view, menus and HUD centred.",
    "4. Look for rendering glitches: seams, wrong shadows, flicker, menus.",
    "5. Music: your music files (choose the folder with A on the Music row),",
    "   your streaming app (Spotify: no game songs under it for a whole ride,",
    "   loading screens too), taking turns (one yours, one the game's).",
    "6. Start > Options while riding: Audio, Display, Extras; B goes back.",
    "7. Earn an achievement: a popup with a chime.",
    "8. Sharp UI: crisp menus, icons and text at 4K; X on Sharp UI remakes it.",
    "9. Controls: swap two controller buttons and check them in game; Reset.",
    "10. Pause > Main Menu > Yes: no freeze. Quick load: LB twice on pause.",
    "11. Sounds: listen for clicks or odd repeats."
) | Set-Content -Encoding ascii (Join-Path $dir "TEST-CHECKLIST.txt")
}

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
