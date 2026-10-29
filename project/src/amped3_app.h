// amped3 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/overlay/achievement_toast.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/ui/window.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "frame_graph.h"
#include "pix_capture.h"
#include "hang_watch.h"
#include "scripted_input.h"

REXCVAR_DECLARE(bool, dump_image);
REXCVAR_DECLARE(int32_t, framerate);
REXCVAR_DECLARE(bool, scripted_input);
REXCVAR_DECLARE(int32_t, hang_watch);
REXCVAR_DECLARE(bool, show_fps);
REXCVAR_DECLARE(bool, pix_gpu_capture);

// Sets a default for a flag unless the command line already names it. GPU plugin
// flags register only when the plugin DLL loads, after OnPreSetup, so those fall
// back to the REX_<NAME> environment variable the SDK applies at registration.
inline void SetDefaultPluginFlag(const char* name, const char* value) {
  if (std::string_view(GetCommandLineA()).find(std::string("--") + name) != std::string_view::npos) {
    return;
  }
  if (rex::cvar::SetFlagByName(name, value)) {
    return;
  }
  std::string env = "REX_";
  for (const char* c = name; *c; ++c) {
    env += char(std::toupper(static_cast<unsigned char>(*c)));
  }
  SetEnvironmentVariableA(env.c_str(), value);
}

// The stock achievement toast dialog stays attached to the ImGui drawer for the
// whole session, and any attached ImGui dialog makes the presenter repaint on
// every display refresh (~200 presents/s against 60 game frames), competing
// with GPU emulation. Attach it only while a toast can be on screen (from
// Table-Tennis-Rockstar-PC, MIT; see THIRD_PARTY_NOTICES.md).
class OnDemandToastDialog : public rex::ui::AchievementToastDialog {
 public:
  OnDemandToastDialog(rex::ui::WindowedAppContext& app_context, rex::ui::ImGuiDrawer* drawer,
                      rex::ui::ImmediateDrawer* immediate_drawer, rex::Runtime* runtime)
      : AchievementToastDialog(drawer, immediate_drawer, runtime), app_context_(app_context) {
    imgui_drawer()->RemoveDialog(this);  // The base constructor attached it.
  }

  void Push(const rex::system::AchievementEvent& event) override {
    AchievementToastDialog::Push(event);
    app_context_.CallInUIThreadDeferred([this]() {
      // Toasts are shown one after another for 4.5 s each.
      const auto now = std::chrono::steady_clock::now();
      visible_until_ = std::max(visible_until_, now) + std::chrono::milliseconds(4700);
      if (!attached_) {
        imgui_drawer()->AddDialog(this);
        attached_ = true;
      }
    });
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    AchievementToastDialog::OnDraw(io);
    if (attached_ && std::chrono::steady_clock::now() > visible_until_) {
      imgui_drawer()->RemoveDialog(this);
      attached_ = false;
    }
  }

 private:
  rex::ui::WindowedAppContext& app_context_;
  std::chrono::steady_clock::time_point visible_until_{};
  bool attached_ = false;
};

class Amped3App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Amped3App>(new Amped3App(ctx, "amped3",
        PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // Debug: load PIX's GPU capturer before the D3D12 device exists; the
    // scripted "pixcapture" command then captures the next frame.
    if (REXCVAR_GET(pix_gpu_capture)) {
      amped3::pix::LoadCapturer();
    }
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
    // Name prompts the game leaves empty (New Rider) start with this, so a
    // controller-only player can confirm with A (patch_sdk_keyboard_pad.py).
    SetDefaultPluginFlag("headless_keyboard_text", "Rider");
    const int32_t fps = REXCVAR_GET(framerate);
    if (fps == 0 || fps > 60) {
      SetDefaultPluginFlag("vsync", "false");
      SetDefaultPluginFlag("d3d12_allow_variable_refresh_rate_and_tearing", "true");
      SetDefaultPluginFlag("clear_memory_page_state", "false");
    }
    if (REXCVAR_GET(scripted_input)) {
      config.input_factory = [](bool tool_mode) -> std::unique_ptr<rex::system::IInputSystem> {
        auto input = rex::input::CreateDefaultInputSystem(tool_mode);
        input->AddDriver(amped3::scripted_input::CreateDriver());
        REXLOG_INFO("Amped3: scripted input driver added");
        return input;
      };
    }
  }

  void OnPostLoadXexImage() override {
    // The game uses the 360 utility partition (CACHE:) for scratch files. The
    // SDK's Harddisk0\Cache0 is a NullDevice, so back it with a host folder.
    if (auto* vfs = runtime()->kernel_state()->file_system()) {
      const auto cache_dir = cache_root() / "Cache0";
      std::filesystem::create_directories(cache_dir);
      auto device = std::make_unique<rex::filesystem::HostPathDevice>(
          "\\Device\\Amped3Cache", cache_dir, false);
      if (device->Initialize() && vfs->RegisterDevice(std::move(device))) {
        vfs->RegisterSymbolicLink("cache:", "\\Device\\Amped3Cache");
        REXLOG_INFO("Amped3: CACHE: mounted at {}", cache_dir.string());
      }
    }
    // Debug: write the decrypted, decompressed image for offline analysis.
    if (REXCVAR_GET(dump_image)) {
      constexpr uint32_t kImageBase = 0x82000000, kImageSize = 0x8F0000;
      auto* image = runtime()->memory()->TranslateVirtual(kImageBase);
      std::ofstream out(rex::filesystem::GetExecutableFolder() / "image.bin", std::ios::binary);
      out.write(reinterpret_cast<const char*>(image), kImageSize);
      REXLOG_INFO("Amped3: dumped image {:08X}+{:X} to image.bin", kImageBase, kImageSize);
    }
  }

  // Window title listing the frame rate, render scale and any other flags on the
  // command line (paths and window plumbing left out), to tell test runs apart.
  static std::string SettingsTitle() {
    std::string title = "Amped 3 | " + std::to_string(REXCVAR_GET(framerate)) + " FPS";
    if (REXCVAR_GET(framerate) == 0) {
      title = "Amped 3 | unlimited FPS";
    }
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
      std::wstring w(argv[i]);
      std::string arg(w.begin(), w.end());
      if (arg.rfind("--", 0) != 0) {
        continue;
      }
      const std::string name = arg.substr(2, arg.find('=') - 2);
      for (const char* skip : {"game_data_root", "log_file", "log_flush_interval", "window_width",
                               "window_height", "mnk_mode", "framerate", "scripted_input",
                               "headless_keyboard_text", "fps_log"}) {
        if (name == skip) {
          arg.clear();
          break;
        }
      }
      if (!arg.empty()) {
        title += " | " + arg.substr(2);
      }
    }
    if (argv) {
      LocalFree(argv);
    }
    return title;
  }

  void OnPostSetup() override {
    amped3::frame_graph::InstallPresentHook();
    // To the front when started by the launcher (which allows it) or from a
    // shortcut; otherwise fullscreen can open behind other windows.
    if (window()) {
      SetForegroundWindow(static_cast<HWND>(window()->GetNativeWindowHandle()));
    }
    if (REXCVAR_GET(hang_watch) > 0) {
      amped3::hang_watch::Start(runtime(), REXCVAR_GET(hang_watch));
    }
  }

  void OnShutdown() override { amped3::hang_watch::Stop(); }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  void OnCreateDialogs(rex::ui::ImGuiDrawer*) override {
    if (window()) {
      window()->SetTitle(SettingsTitle());
    }
    if (REXCVAR_GET(show_fps) && window()) {
      amped3::frame_graph::Show(window()->GetNativeWindowHandle(), REXCVAR_GET(framerate));
    }
    rex::ui::RegisterBind("bind_frame_graph", "F10", "Toggle the frame-time graph", [this] {
      if (amped3::frame_graph::IsVisible()) {
        amped3::frame_graph::Hide();
      } else if (window()) {
        amped3::frame_graph::Show(window()->GetNativeWindowHandle(), REXCVAR_GET(framerate));
      }
    });
  }
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;

  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog()
      override {
    if (!imgui_drawer() || !immediate_drawer() || !runtime()) {
      return nullptr;
    }
    return std::make_unique<OnDemandToastDialog>(app_context(), imgui_drawer(), immediate_drawer(),
                                                 runtime());
  }
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
