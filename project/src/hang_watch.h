// hang_watch.h - Debug watchdog that logs where every guest thread is
//
// With --hang_watch=N, every N seconds each guest thread's lr, ctr, r3 and a
// short back-chain of return addresses are logged, so a hung thread shows the
// guest code that blocked.

#pragma once

namespace rex {
class Runtime;
}

namespace amped3::hang_watch {

void Start(rex::Runtime* runtime, int interval_seconds);
void Stop();

}  // namespace amped3::hang_watch
