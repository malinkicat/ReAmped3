# ReAmped 3

A native Windows port of **Amped 3** (Xbox 360, 2005) made by static recompilation with [ReXGlue](https://github.com/rexglue). The game's PowerPC code is translated to x64 C++ and built into a normal Windows program, running on a modified ReXGlue runtime with Direct3D 12.

No game files are included. You need your own copy of the game.

## Status

Playable from boot through the story, menus, rider creation and loading, the mountain map and runs. No known crashes. Still a work in progress.

## What's different from the Xbox 360 version

- **Resolution:** 720p, 1080p, 1440p (2x internal) or 4K (3x internal).
- **Frame rate:** 30 (original), 60 (default), any cap (e.g. 141 for a 144 Hz VRR screen), or unlimited. Gameplay runs at the correct speed at any frame rate. Story cutscenes hold 60 so they stay in sync.
- **Frame pacing:** steady frame times at 60 (16.67 ms ± 0.05) and at capped rates with G-Sync/FreeSync.
- **Image options:** FSR 1, CAS or bilinear scaling, optional FXAA.
- **Launcher:** pick the game folder, resolution, frame rate (including "match the display"), fullscreen and filters. Settings are saved.
- **F10** shows a frame time graph.
- The new-rider name prompt works with a controller.

## Fixes over running it as-is

- UI drawn as vertical stripes (a texture fetch translation bug, same fix as Xenia Canary).
- Two music tracks playing at once after loading (the game dropped fade requests that arrived during a fade).
- The legal screen hanging on start-up about two runs in three (a thread shutdown race in the game).
- Cutscenes running too fast above 60 FPS.
- Much faster rendering at high resolution and frame rate: render target and resolve handling reworked (direct resolves for this game's 2x MSAA tiled rendering), and a lot of per-draw overhead removed from the GPU command processor. On an RTX 2080 Ti at 1x the game went from about 100 to about 200 FPS uncapped.

## Known issues

- Above 30 FPS the other riders sometimes wipe out at the start of a run, and their tricks can look quicker than they should. Cloth on rider clothing moves faster than intended. Being worked on.
- Loading screens can stall for 1-2 seconds.
- In very busy areas (10,000+ draw calls per frame) the CPU side still limits the frame rate to around 120-140 on a fast PC.

## What you need

- Windows 10/11 x64, a CPU with AVX2, a Direct3D 12 GPU.
- Your own Amped 3 (USA) disc, extracted to a folder (the one with `default.xex`).

## Building

Only a developer build exists for now. It needs:

- Visual Studio 2022 Build Tools (C++, Windows SDK, CMake, Ninja) and LLVM/Clang.
- The ReXGlue SDK v0.10.0 in `.tools/sdk` (headers, libraries and `rexglue.exe` for code generation).
- The modified ReXGlue runtime (`rexruntime.dll`, `rexgpu-xenos.dll`). Its source will be published separately; the stock runtime is missing the fixes above.

Steps:

1. Copy `tools\local.cmd.example` to `tools\local.cmd` and set your game folder, LLVM and SDK source paths.
2. Copy `default.xex` from your game folder to `private_work\`.
3. Generate the code: in `project\`, run `..\.tools\sdk\win-amd64\bin\rexglue.exe codegen amped3_manifest.toml`.
4. Put the runtime DLLs in `project\sdk-bin\`, then run `tools\build.cmd` and `tools\build_launcher.cmd`.
5. Start `build\amped3_launcher.exe` and point it at your game folder.

Never upload `build`, `dist`, `private_work` or `project/generated`. They contain or are made from game files.

## Layout

- `project/overrides.toml`: functions the analyser missed, and hooks into the game code.
- `project/src`: the hooks (frame rate, music, shutdown fix) and opt-in debug tools.
- `project/launcher_sdl`: the settings launcher (SDL3 and Dear ImGui; works with a controller, also on the Steam Deck). `project/launcher`: the earlier WPF launcher, no longer built.
- `tools`: build, test and profiling scripts.

## Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk), the recompiler and runtime, and [Xenia](https://github.com/xenia-canary/xenia-canary), which the runtime is based on.
- [iExplosiveRage's ReXGlue fork](https://github.com/iExplosiveRage/rexglue-sdk) for the render target and resolve work this builds on.
- [Table-Tennis-Rockstar-PC](https://github.com/davidptrssvea/Table-Tennis-Rockstar-PC) for the frame graph, the launcher's starting point and a few runtime fixes.
- Brian Maguire for finding the cause of the UI corruption, and [Xenia Canary PR #1129](https://github.com/xenia-canary/xenia-canary/pull/1129) for the fix.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details and licenses.

## License

MIT for this project's own code (see [LICENSE](LICENSE)). Third-party parts keep their own licenses ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). Game content is not included or covered.

Amped 3 is © 2K Games / Indie Built. This project is not affiliated with them.
