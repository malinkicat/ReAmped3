// ReAmped 3 launcher: game folder and settings, then starts amped3.exe with
// them (the same settings.ini and command line as the earlier WPF launcher).
//   amped3_launcher.exe [game args...]   arguments are passed on to the game
//   amped3_launcher.exe --selftest       checks of the pure logic
#include <windows.h>

#include <string>

#include "system.h"

int RunSelfTest();
int RunLauncher();

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  for (const std::string& a : sys::LauncherArgs()) {
    if (a == "--selftest") return RunSelfTest();
  }
  return RunLauncher();
}
