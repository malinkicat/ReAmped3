"""ReXGlue v0.10.0 patch: fire the guest vblank on time instead of on a Sleep(1) loop.

The vblank thread polled with Sleep(1 ms), whose real length on Windows varies,
so vblanks (and the game's flips, which wait for them) landed +-2-3 ms off:
frame times 14-20 ms at 60 FPS. Now it waits on a high-resolution waitable timer
until ~0.5 ms before the next vblank and spins the rest.

usage: patch_sdk_vblank_timer.py <rexglue-src>
"""
import sys
from pathlib import Path

path = Path(sys.argv[1]) / 'src/graphics/graphics_system.cpp'
src = path.read_text()
if 'CREATE_WAITABLE_TIMER_HIGH_RESOLUTION' in src:
    print('graphics_system.cpp: already patched')
    sys.exit(0)

inc_old = '#include <rex/ui/windowed_app_context.h>\n'
inc_new = inc_old + '''
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
'''
loop_old = '''        uint64_t last_frame_time = chrono::Clock::QueryGuestTickCount();
        while (vsync_worker_running_) {
          uint64_t current_time = chrono::Clock::QueryGuestTickCount();
          uint64_t interval_ticks =
              REXCVAR_GET(vsync) ? vsync_interval_ticks : no_vsync_interval_ticks;
          while (current_time - last_frame_time >= interval_ticks) {
            MarkVblank();
            last_frame_time += interval_ticks;
          }
          rex::thread::Sleep(std::chrono::milliseconds(1));
        }
        return 0;'''
loop_new = '''        uint64_t last_frame_time = chrono::Clock::QueryGuestTickCount();
#ifdef _WIN32
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr,
                                              CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                              TIMER_ALL_ACCESS);
#endif
        // Wake this long before a vblank and spin the rest, for timer slack.
        const uint64_t spin_ticks = guest_tick_frequency / 2000;  // 0.5 ms
        while (vsync_worker_running_) {
          uint64_t current_time = chrono::Clock::QueryGuestTickCount();
          uint64_t interval_ticks =
              REXCVAR_GET(vsync) ? vsync_interval_ticks : no_vsync_interval_ticks;
          while (current_time - last_frame_time >= interval_ticks) {
            MarkVblank();
            last_frame_time += interval_ticks;
          }
          const uint64_t remaining = last_frame_time + interval_ticks - current_time;
          // Spin only for real vblanks; with vsync off they come every 1 ms and
          // spinning half of each would keep a core busy.
          const uint64_t spin = interval_ticks >= guest_tick_frequency / 200 ? spin_ticks : 0;
          if (remaining > spin) {
#ifdef _WIN32
            LARGE_INTEGER due;
            due.QuadPart = -int64_t((remaining - spin) * 10000000 / guest_tick_frequency);
            if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
              WaitForSingleObject(timer, INFINITE);
            } else {
              rex::thread::Sleep(std::chrono::milliseconds(1));
            }
#else
            rex::thread::Sleep(std::chrono::microseconds(
                (remaining - spin) * 1000000 / guest_tick_frequency));
#endif
          } else {
            rex::thread::MaybeYield();
          }
        }
#ifdef _WIN32
        if (timer) {
          CloseHandle(timer);
        }
#endif
        return 0;'''
assert src.count(inc_old) == 1 and src.count(loop_old) == 1, 'anchors not found'
src = src.replace(inc_old, inc_new).replace(loop_old, loop_new)
path.write_text(src)
print('graphics_system.cpp: patched')
