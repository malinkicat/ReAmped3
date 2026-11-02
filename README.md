# ReAmped 3

Amped 3 (Xbox 360, 2005) running natively on Windows and the Steam Deck.

It's a static recompilation: the game's PowerPC code is translated to x64 C++ with [ReXGlue](https://github.com/rexglue) and built as a normal program, on our modified ReXGlue runtime (Direct3D 12). It's not an emulator and no game files are included. You need your own copy of the game.

**Status:** playable from boot through the story: menus, rider creation, the open mountain and events. No known crashes (45-minute PC and 25-minute Steam Deck soak tests clean).

## What we added

**Launcher**
- Game folder check (tells you if it's the tested USA version).
- Resolution from the list your display supports, or "Match the display". 720p, 1440p or 4K internal.
- Frame rate: 30 (original), 60, 40/90 on the Deck, any cap, or unlimited. The game runs at the right speed at all of them.
- Anti-aliasing (SMAA, FXAA or none) and sharpening (AMD CAS).
- Fast single-pass rendering (on by default, see below).
- Sharp UI: upscales the game's own menus, fonts and HUD art 4x on your PC (2x on the Deck) with Real-ESRGAN, an offline image upscaler. Runs once, about 5 minutes, and is kept. Nothing is downloaded or uploaded and no game art is shipped.
- UI size: everything smaller, or each part on its own (menus, HUD, help tips, radio popup, button legends). On the Deck the HUD and tips can go up to 150%.
- Controls: remap pad and keyboard, each row says what it does in the game.
- Music source (see below), ReAmped logo option, and Recommended settings on the Steam Deck (full 1280x800, 60 FPS, SMAA, help tips 130%).
- Works with a controller, built for the Deck too.

**In the game**
- Ultrawide and 16:10: a wider (or taller) view of the mountain, nothing stretched, HUD and menus kept 16:9.
- Starts fast: the publisher movies are skipped and the logo/warning screens are one loading screen.
- Your own music: your music files (a folder you pick) or your streaming app (Spotify etc., Windows media controls or MPRIS on Linux). It only plays where the game's soundtrack would (riding and the in-game menus, not the title, loading, cutscenes or event jingles). Tracks show in the game's own radio popup and the D-pad skips them.
- ReAmped 3 settings inside the game's own Options menu: Audio, Display, Extras.
- Achievements: popup, chime and a list with progress.
- Quick save (Back on the pause screen) and quick load (LB twice).
- Save backups (last 10 kept, restore from the menu).
- Controller on-screen keyboard for the new-rider name.
- ReAmped logo on the title and loading screens (opt-in; off, it's the original Amped 3 logo).
- F10 frame time graph.

## What we fixed

- UI drawn as vertical stripes (texture fetch translation bug, same fix as Xenia Canary).
- Two music tracks playing at once after loading (the game dropped fade requests during a fade).
- Start-up hanging on the legal screen about 2 runs in 3 (a thread shutdown race in the game).
- Cutscenes running too fast above 60 FPS.
- Sled turning and sled crash scoring tied to the frame rate (fixes found by Amped 3 Revamped).
- A 2.7 s freeze leaving the mountain (the game waited out the 360's minimum save-message time without drawing).
- Looping sounds that never looped (XMA loop points read as the wrong structure, inherited from Xenia).
- The "stuck, press Back" tip misfiring at high frame rates.
- Speed: render targets and resolves reworked, per-draw overhead cut, and the game's hot matrix maths run as native code (bit-identical). About 100 to 200 FPS uncapped on an RTX 2080 Ti.
- Single-pass rendering: the 360 drew each frame in 3 strips to fit its 10 MB of video memory. A PC doesn't need that, so we draw it once. Much less CPU work (the Deck holds 60 in the busiest spot). The catch: the game's own 2x MSAA goes with it, so SMAA is on by default. You can turn it off in the launcher.

## Performance

Measured at the busiest view in the game: the top of Loveseat on Northstar's open mountain, about 12,000-15,000 draws a frame. Riding elsewhere is lighter.

**PC: RTX 2080 Ti, Ryzen 9 3900X** (1280x720, frame rate unlocked)

| | Average | 1% low |
|---|---|---|
| Single-pass rendering on (default) | 159 FPS | 120 FPS |
| Single-pass off (the game's own 2x MSAA) | 65 FPS | 58 FPS |

The busiest views are limited by the CPU (the game's main thread and the GPU emulation), not the graphics card.

**Steam Deck OLED, Proton 10** (the launcher's Recommended settings: full 1280x800 screen, SMAA, single-pass)

| Frame rate setting | Average | 1% low |
|---|---|---|
| 60 FPS | 60.0 FPS | 59.0 FPS (locked) |
| 90 FPS | 86.5 FPS | 55.2 FPS |
| Unlocked | 91.9 FPS | 53.5 FPS |

Without single-pass rendering the Deck manages 58.7 FPS (1% low 45) at a 60 cap in that spot. Ordinary riding averages about 90 at the 90 FPS setting.

## Changes to other projects

**ReXGlue runtime** (our fork of [rexglue-sdk](https://github.com/rexglue/rexglue-sdk), BSD-3, published separately):
- GPU: single-pass "tile collapse", MSAA/D24FS8/strip resolves for this game, a two-thread command processor, cheaper register/constant/texture handling, hot-page tracking, pipeline reuse.
- Image: SMAA, AMD FSR/CAS sharpening, ultrawide/16:10 squeeze, UI scale. NVIDIA DLAA and FSR 3 frame generation are in the code but switched off until the game has per-object motion vectors.
- Texture replacement: hashed lookups, background loading, alpha fixes (what Sharp UI uses).
- Audio: your-own-music player (FLAC/MP3/WAV/OGG), soundtrack-aware hold, XMA loop fix.
- Files: a data overlay so patched UI files load without touching your game folder.
- Input and system: controller on-screen keyboard, mouse/keyboard driver fixes, Wine/Proton timer and threading tweaks.
- Ideas from [Project8Recomp](https://github.com/theokyr/Project8Recomp) for the Deck (bulk register writes, blocking timer wait).
- Built on [iExplosiveRage's fork](https://github.com/iExplosiveRage/rexglue-sdk) work (render targets, resolves, texture dump/replace).

**Game data at run time** (made on your PC from your files, never shipped): patched UI scenes (sizes, ReAmped menus, quick save legends), the loading screen and the optional logo.

## Known issues

- Above 30 FPS other riders sometimes wipe out at the start of a run.
- Frame generation isn't offered: the game only gives camera motion, so riders and the HUD double up.
- Only the USA disc is tested.
- Very busy spots are CPU-bound at about 120-140 FPS on a fast PC.
- The dark backing behind some menu titles (LOAD A RIDER) shows steps at 4K: it's a rough mesh in the game itself.
- Steam Deck: short pauses on loading screens. Riding holds 60.

## What you need

- Windows 10/11 x64 with an AVX2 CPU and a Direct3D 12 GPU, or a Steam Deck / Linux PC with Steam's Proton 10 (add the package's `run.sh` to Steam as a non-Steam game; its README.txt has the steps).
- Your own Amped 3 disc, extracted (the folder with `default.xex`).

## Building

Developer build only for now. Needs Visual Studio 2022 Build Tools (C++, Windows SDK, CMake, Ninja), LLVM/Clang, the ReXGlue SDK v0.10.0 in `.tools/sdk`, and our runtime fork.

1. Copy `tools\local.cmd.example` to `tools\local.cmd` and set your game folder, LLVM and runtime source paths.
2. Copy `default.xex` from your game folder to `private_work\`.
3. In `project\`, run `..\.tools\sdk\win-amd64\bin\rexglue.exe codegen amped3_manifest.toml`.
4. Build the runtime with `tools\build_sdk_ffx.cmd` (puts the DLLs in `project\sdk-bin\`), then `tools\build.cmd` and `tools\build_launcher.cmd`.
5. Run `build\amped3_launcher.exe` and point it at your game folder.

Never upload `build`, `dist`, `private_work` or `project/generated`: they contain or are made from game files.

## Layout

- `project/overrides.toml`: hooks into the game code and functions the analyser missed.
- `project/src`: the hooks and features (frame rate, music, UI patch, menus, saves, achievements).
- `project/launcher_sdl`: the launcher (SDL3 + Dear ImGui).
- `tools`: build, packaging, test and benchmark scripts (PC and Steam Deck).

## Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) and [Xenia](https://github.com/xenia-canary/xenia-canary), which the runtime comes from.
- [iExplosiveRage's ReXGlue fork](https://github.com/iExplosiveRage/rexglue-sdk): render target and resolve work, texture dump/replace.
- [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) and [Real-ESRGAN-ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan) (Xintao Wang): the upscaler.
- [Table-Tennis-Rockstar-PC](https://github.com/davidptrssvea/Table-Tennis-Rockstar-PC): the frame graph, the launcher's starting point and some runtime fixes.
- [Amped 3 Revamped](https://github.com/minty-hippo/amped3revamped) for the sled frame-rate fixes.
- Brian Maguire for finding the UI corruption cause, and [Xenia Canary PR #1129](https://github.com/xenia-canary/xenia-canary/pull/1129) for the fix.

Details and licences: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

MIT for this project's own code ([LICENSE](LICENSE)). Third-party parts keep their own licences. Game content isn't included or covered.

Amped 3 is © 2K Games / Indie Built. This project isn't affiliated with them.
