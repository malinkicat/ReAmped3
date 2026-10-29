// pix_capture.cpp - Debug: GPU captures with PIX from inside the game
//
// PIX can only capture a process that loaded WinPixGpuCapturer.dll before it
// created its D3D12 device; the DLL exports CaptureNextFrame (PIXGpuCaptureNextFrames in pix3.h), which the
// scripted-input command "pixcapture N" calls at the moment of interest.
// Analyse with: pixtool open-capture <file> save-event-list <csv> --counters=*

#include "pix_capture.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <rex/filesystem.h>
#include <rex/logging.h>

#include <ctime>
#include <filesystem>
#include <string>

namespace amped3::pix {
namespace {
HMODULE g_capturer = nullptr;
}  // namespace

void LoadCapturer() {
  const std::filesystem::path root = L"C:\\Program Files\\Microsoft PIX";
  std::filesystem::path newest;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (std::filesystem::exists(entry.path() / L"WinPixGpuCapturer.dll") && entry.path() > newest) {
      newest = entry.path();
    }
  }
  if (newest.empty()) {
    REXLOG_WARN("Amped3: no PIX install with WinPixGpuCapturer.dll under {}", root.string());
    return;
  }
  g_capturer = LoadLibraryW((newest / L"WinPixGpuCapturer.dll").c_str());
  REXLOG_INFO("Amped3: PIX GPU capturer {} from {}", g_capturer ? "loaded" : "failed to load",
              newest.string());
}

void CaptureNextFrames(int frames) {
  using CaptureFn = HRESULT(WINAPI*)(PCWSTR, UINT32);
  auto capture = g_capturer
                     ? reinterpret_cast<CaptureFn>(GetProcAddress(g_capturer, "CaptureNextFrame"))
                     : nullptr;
  if (!capture) {
    REXLOG_WARN("Amped3: pixcapture needs --pix_gpu_capture=true");
    return;
  }
  const auto path = rex::filesystem::GetExecutableFolder() / ".." / "private_work" /
                    ("gpu_" + std::to_string(std::time(nullptr)) + ".wpix");
  const HRESULT hr = capture(path.wstring().c_str(), UINT32(frames));
  REXLOG_INFO("Amped3: PIX capture of {} frame(s) to {}: {:08X}", frames, path.string(), uint32_t(hr));
}

}  // namespace amped3::pix
