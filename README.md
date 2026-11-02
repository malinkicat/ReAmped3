# ReAmped 3

A PC port of Amped 3 (Xbox 360, 2005) for Windows 11 and the Steam Deck.

Performance has been the main focus. On an RTX 2080 Ti with a Ryzen 9 3900X it does 144 FPS at 4K in one of the less performant areas of the game, and on the Steam Deck it's near locked at 60 in the same spot. You can set any frame rate from the original 30 up to unlocked and the game still runs at the right speed. More on that under [Performance](#performance).

It's a hybrid of static recompilation and emulation. The game's own PowerPC code is translated to x64 C++ with [ReXGlue](https://github.com/rexglue) and built into a normal Windows program, so it runs natively. The rest of the Xbox 360 (the GPU, audio and system) is still emulated by the ReXGlue runtime, which comes from Xenia, and that's where most of our changes are. No game files are included, so you'll need your own copy.

You can play through the story, make riders, ride the open mountain and do events. It's had hours of real play on PC and Steam Deck plus long automated soak runs, and there are no known crashes.

## Installing

1. Grab the latest zip from the [Releases page](https://github.com/malinkicat/ReAmped3/releases) and unzip it anywhere.
2. Run `amped3_launcher.exe`, pick your extracted Amped 3 folder (the one with `default.xex`), set your options and hit RIDE.

Steam Deck or a Linux PC: it runs through Steam's Proton 10, so you need Steam installed (the normal version, not the Flatpak one for now). The zip's README.txt has the steps (you add `ReAmped3.sh` to Steam as a non-Steam game). It's only been tested on the Steam Deck so far.

If you'd rather build it yourself from source, see [Building from source](#building-from-source).

## What we added

Launcher
- Checks your game folder and tells you if it's the tested USA version.
- Resolution from your display's own list, or "Match the display".
- Frame rate on PC: 30 (the original), 60, 90, 100, 120, 144, Auto (matches your display's refresh rate), your own cap anywhere from 30 to 1000, or unlocked. On the Steam Deck: 30, 40, 60, 90, your own cap or unlocked.
- Anti-aliasing (SMAA, FXAA or none) and sharpening.
- Fast single-pass rendering, on by default (see Performance). Turn it off and you get the game's own 2x MSAA back instead.
- Sharp UI: the game's own menus, fonts and HUD art upscaled 4x on your PC (2x on the Deck) with Real-ESRGAN. It's an offline image upscaler, runs once (about 5 minutes) and keeps the result. Nothing gets downloaded or uploaded and we don't ship any game art.
- UI size: shrink everything, or each part on its own (menus, HUD, help tips, radio popup, button legends). On the Deck the HUD and tips can go up to 150%.
- Controls: remap the pad and keyboard, and each row tells you what it does in the game.
- Music, the ReAmped logo option, and Recommended settings for the Steam Deck.
- Works with a controller, including on the Deck.

In the game
- Ultrawide and 16:10: you see more of the mountain, nothing is stretched, and the HUD and menus stay 16:9.
- Starts quick: the publisher movies are skipped and the logo and warning screens are one loading screen.
- Your own music, either from a folder or your streaming app (Spotify etc.). It only plays where the game's soundtrack would, so riding and the in-game menus, not the title, loading screens, cutscenes or event jingles. Tracks show up in the game's own radio popup and the D-pad skips them.
- ReAmped 3 settings inside the game's own Options menu (Audio, Display, Extras).
- Achievements, with a popup, a chime and a list showing your progress.
- Save backups, the last 10 are kept and you can restore one from the menu.
- On-screen keyboard for naming a new rider with a controller.
- ReAmped logo on the title and loading screens if you want it (off by default, so you get the original Amped 3 logo).
- F10 shows a frame time graph.

## What we fixed

- UI drawn as vertical stripes (texture fetch bug, same fix as Xenia Canary).
- Two music tracks playing at once after loading.
- The game hanging on the legal screen about 2 starts in 3.
- Cutscenes playing too fast above 60 FPS.
- Sled turning and sled crash scores depending on frame rate (fixes from Amped 3 Revamped).
- A 2.7 second freeze when leaving the mountain.
- Looping sounds that never looped.
- The "stuck, press Back" tip popping up at high frame rates.

## Performance

These numbers are from one of the less performant areas of the game: the top of Loveseat on Northstar, where the game draws 12,000 to 15,000 things a frame. We stand still there so every run sees the same thing. Normal riding is lighter than this.

PC: RTX 2080 Ti, Ryzen 9 3900X, Windows 11, frame rate unlocked, default settings (SMAA, single-pass on)

| | Average | 1% low |
|---|---|---|
| 4K | 144 FPS | 111 FPS |
| Single-pass off | 65 FPS | 58 FPS |

At 4K the graphics card isn't the limit here, the CPU is (the game's own main thread and the emulated GPU).

Steam Deck OLED, Proton 10, Recommended settings (full 1280x800 screen, SMAA, single-pass on)

| Frame rate setting | Average | 1% low |
|---|---|---|
| 60 FPS | 60.0 FPS | 59.0 FPS |
| 90 FPS | 86.5 FPS | 55.2 FPS |
| Unlocked | 91.9 FPS | 53.5 FPS |

Without single-pass the Deck can't hold 60 there (58.7 average, 1% low 45). Normal riding sits around 90 on the 90 FPS setting.

### How we got there

Single-pass rendering. The Xbox 360 only had 10 MB of fast video memory, so the game draws every frame in three strips, which means sending the whole scene three times. A PC doesn't have that limit, so we draw it once. That more than doubles the frame rate in busy areas, and it's what gets the Deck to a near-locked 60.

Making the emulated GPU faster. The game still talks to an emulated Xbox 360 GPU, and that was the bottleneck, so we went through it draw by draw:
- render targets and resolves handled directly for how this game draws (building on iExplosiveRage's ReXGlue work)
- register writes done in bulk, shader constants reused when they haven't changed, texture and sampler state cached, pipelines reused
- memory the game rewrites every frame isn't trapped on every write any more
- frames presented straight from the submission thread

Native code for the game's heaviest maths. Its matrix multiply, point transform, visibility test, memory copy and decompression run as our own x64 code instead of translated PowerPC. The results are bit-identical, checked over tens of millions of calls while playing.

Steam Deck tuning. Under Proton the emulated GPU runs on two threads and waits sleep instead of spinning, which leaves the Deck's four cores free for the game. Frame pacing is tuned so 60 and 90 stay even.

Direct3D 12 over Vulkan. We tried ReXGlue's Vulkan backend on the Deck and it only got 54 FPS in that same area, where our Direct3D 12 version (through Proton) stays near-locked at 60. So Direct3D 12 is what we ship on both Windows and the Deck.

Every change was benchmarked in that same spot on the PC and the Deck and only kept if it was faster, then soak tested.

## Changes to other projects

ReXGlue runtime: our fork of [rexglue-sdk](https://github.com/rexglue/rexglue-sdk) (BSD-3) is at [malinkicat/rexglue-sdk, branch `amped3-gpu`](https://github.com/malinkicat/rexglue-sdk/tree/amped3-gpu), and its `REAMPED3.md` lists the changes. In short:
- GPU: single-pass rendering, resolves for this game, two-thread command processor, the speed-ups above.
- Image: SMAA, AMD FSR/CAS sharpening, ultrawide and 16:10, UI scale. NVIDIA DLAA and FSR 3 frame generation are in there but switched off until the game has proper motion vectors.
- Texture replacement with background loading (what Sharp UI uses).
- Audio: your own music player (FLAC, MP3, WAV, OGG) and the looping sound fix.
- A data overlay so our patched UI files load without touching your game folder.
- On-screen keyboard for controllers, keyboard/mouse fixes, and Wine/Proton timer and threading tweaks.
- Some Deck ideas from [Project8Recomp](https://github.com/theokyr/Project8Recomp) (bulk register writes, blocking timer wait).

Nothing from the game is shipped. The patched menus, the loading screen and the optional logo are all made on your PC from your own game files.

## Known issues

- Above 30 FPS the other riders sometimes wipe out at the start of a run.
- No frame generation yet. The game only gives us camera motion, so riders and the HUD double up.
- Only the USA disc has been tested.
- The busiest spots are CPU limited, around 140 FPS on a fast PC.
- The dark shape behind some menu titles (like LOAD A RIDER) looks a bit jagged at 4K. It's a rough mesh in the game itself.
- Steam Deck: short pauses on loading screens. Riding itself stays smooth.

## What you need

- Windows 11 x64, a CPU with AVX2 and a Direct3D 12 graphics card. Or a Steam Deck, or a Linux PC with Steam and Proton 10.
- Your own Amped 3 disc, extracted (the folder with `default.xex`).

## Building from source

You'll need:
- Visual Studio 2022 Build Tools with "Desktop development with C++" (includes the Windows SDK, CMake and Ninja), installed in the default place.
- LLVM/Clang for Windows (we use 19.1.7).
- Git.
- Your extracted Amped 3 folder.

Steps:

1. Clone this repo, and our runtime next to it:
   ```
   git clone https://github.com/malinkicat/ReAmped3.git
   git clone --recursive -b amped3-gpu https://github.com/malinkicat/rexglue-sdk.git
   ```
2. Download `rexglue-sdk-0.10.0-win-amd64.zip` from [ReXGlue's v0.10.0 release](https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.0) and unzip it so you end up with `ReAmped3\.tools\sdk\win-amd64\bin\rexglue.exe`.
3. Copy `tools\local.cmd.example` to `tools\local.cmd` and set `AMPED3_GAME_DIR` (your game folder), `LLVM` (Clang's `bin` folder) and `SDK_SRC` (where you cloned rexglue-sdk).
4. Build the runtime: `tools\build_sdk_ffx.cmd`. The first run downloads AMD FidelityFX and the NVIDIA DLSS SDK files it builds against, and takes a while. It puts the DLLs in `project\sdk-bin`.
5. Copy `default.xex` from your game folder into `private_work\`, then in `project\` run:
   ```
   ..\.tools\sdk\win-amd64\bin\rexglue.exe codegen amped3_manifest.toml
   ```
6. Build the game and the launcher: `tools\build.cmd`, then `tools\build_launcher.cmd`.
7. Run `build\amped3_launcher.exe` and point it at your game folder.

To make a zip like the release: download [Real-ESRGAN-ncnn-vulkan v0.2.5.0](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan/releases/tag/v0.2.5.0) for Windows, unzip it into `private_work\esrgan`, then run `powershell -File tools\package.ps1 -Release`. The zip ends up in `dist\`.

Never upload `build`, `dist`, `private_work` or `project/generated`. They contain or are made from game files.

## Layout

- `project/overrides.toml`: hooks into the game code and functions the analyser missed.
- `project/src`: our hooks and features (frame rate, music, UI patch, menus, saves, achievements).
- `project/launcher_sdl`: the launcher (SDL3 + Dear ImGui).
- `tools`: build, packaging, test and benchmark scripts.

## Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) and [Xenia](https://github.com/xenia-canary/xenia-canary), which the runtime comes from.
- [iExplosiveRage's ReXGlue fork](https://github.com/iExplosiveRage/rexglue-sdk) for render target and resolve work and texture replacement.
- [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) and [Real-ESRGAN-ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan) (Xintao Wang) for the upscaler.
- [Table-Tennis-Rockstar-PC](https://github.com/davidptrssvea/Table-Tennis-Rockstar-PC) for the frame graph, the launcher's starting point and some runtime fixes.
- [Amped 3 Revamped](https://github.com/minty-hippo/amped3revamped) for the sled frame rate fixes.
- Brian Maguire for finding the cause of the UI corruption, and [Xenia Canary PR #1129](https://github.com/xenia-canary/xenia-canary/pull/1129) for the fix.

AI was used to help make this, for writing and reviewing code, research and testing. It's all been tested on a real PC and Steam Deck.

Full details and licences are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

MIT for our own code ([LICENSE](LICENSE)). Third-party parts keep their own licences. Game content isn't included or covered.

Amped 3 is © 2K Games / Indie Built. This project isn't affiliated with them.
