// ReAmped 3 launcher: game folder and settings, then starts amped3.exe with
// them (the same settings.ini and command line as the earlier WPF launcher).
//   amped3_launcher.exe [game args...]   arguments are passed on to the game
//   amped3_launcher.exe --selftest       checks of the pure logic
//   amped3_launcher.exe --install-vendor ID [--accept-license]
//                                         fetch a vendor package (logic.h) from a
//                                         console; without --accept-license it
//                                         only names the license to accept
//   amped3_launcher.exe --offer-vendor ID   the launcher, opening that package's
//                                         license screen (testing)
//   amped3_launcher.exe --open-controls   the launcher on its Controls page (testing)
//   amped3_launcher.exe --build-ui-pack GAME_DIR TEXTURES_DIR [SCALE]
//                                         make the sharp UI pack from a console
//                                         (ui_pack.h; testing)
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "system.h"
#include "ui_pack.h"
#include "vendor.h"

int RunSelfTest();
int RunLauncher();

namespace {

int RunInstallVendor(const std::string& id, bool accepted) {
  sys::AttachParentConsole();
  const launcher::VendorPackage* p = launcher::FindVendorPackage(id);
  if (!p) {
    std::printf("unknown package: %s\n", id.c_str());
    return 2;
  }
  const std::string dist = sys::ExeDir();
  if (!accepted) {
    std::printf("%s is under the %s (%s). Add --accept-license to accept it and download.\n", p->name.c_str(),
                p->license_name.c_str(), p->license.url.c_str());
    return 3;
  }
  std::vector<launcher::VendorFile> files = {p->license};
  files.insert(files.end(), p->files.begin(), p->files.end());
  for (const launcher::VendorFile& f : files) {
    std::string error;
    int last = -1;
    bool ok = vendor::Fetch(*p, f, dist, [&](unsigned long long done, unsigned long long total) {
      int pct = total ? int(done * 100 / total) : 0;
      if (pct / 10 != last / 10) std::printf("%s %d%%\n", f.name.c_str(), pct);
      last = pct;
      return true;
    }, error);
    if (!ok) {
      std::printf("%s: %s\n", f.name.c_str(), error.c_str());
      return 1;
    }
  }
  std::printf("%s installed in %s\n", p->name.c_str(), launcher::VendorDir(dist, p->id).c_str());
  return vendor::Installed(*p, dist) ? 0 : 1;
}

int RunBuildUiPack(const std::string& game_dir, const std::string& textures_dir, int scale) {
  sys::AttachParentConsole();
  uipack::Options o;
  o.game_dir = game_dir;
  o.textures_dir = textures_dir;
  o.esrgan_dir = launcher::JoinPath(launcher::JoinPath(sys::ExeDir(), "tools"), "esrgan");
  o.scale = scale;
  if (!uipack::UpscalerPresent(o.esrgan_dir)) {
    std::printf("the upscaler isn't in %s\n", o.esrgan_dir.c_str());
    return 2;
  }
  uipack::Progress progress;
  std::atomic<bool> cancel{false};
  std::string error;
  const DWORD started = GetTickCount();
  const bool ok = uipack::Build(o, progress, cancel, error);
  std::printf("%s: %d of %d textures in %.1f s%s%s\n", ok ? "done" : "failed", progress.done.load(),
              progress.total.load(), (GetTickCount() - started) / 1000.0, ok ? "" : ", ", error.c_str());
  return ok ? 0 : 1;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  const std::vector<std::string> args = sys::LauncherArgs();
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--selftest") return RunSelfTest();
    if (args[i] == "--build-ui-pack" && i + 2 < args.size()) {
      return RunBuildUiPack(args[i + 1], args[i + 2], i + 3 < args.size() ? std::atoi(args[i + 3].c_str()) : 4);
    }
    if (args[i] == "--install-vendor" && i + 1 < args.size()) {
      bool accepted = false;
      for (const std::string& a : args) accepted |= a == "--accept-license";
      return RunInstallVendor(args[i + 1], accepted);
    }
  }
  if (!sys::ClaimSingleInstance()) return 0;
  return RunLauncher();
}
