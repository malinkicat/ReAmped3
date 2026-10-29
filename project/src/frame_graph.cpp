// frame_graph.cpp - On-screen frame-time graph (F10 or --show_fps=true)
//
// Window and present hook adapted from Table-Tennis-Rockstar-PC's stats_window.cpp
// (github.com/davidptrssvea/Table-Tennis-Rockstar-PC, MIT; see THIRD_PARTY_NOTICES.md)
// and frame_pacer.cpp.

#include "frame_graph.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

REXCVAR_DECLARE(bool, fps_log);

namespace amped3::frame_graph {
namespace {

constexpr size_t kHistoryFrames = 240;
constexpr size_t kSummaryFrames = 120;

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct SeriesStats {
  float fps = 0.0f, avg_ms = 0.0f, p99_ms = 0.0f, max_ms = 0.0f;
};

// Frame intervals of one series, written by a single thread.
class History {
 public:
  void Record() {
    const int64_t now = NowNs();
    if (last_ns_) {
      std::lock_guard lock(mutex_);
      values_[next_ % values_.size()] = float(double(now - last_ns_) / 1e6);
      ++next_;
    }
    last_ns_ = now;
  }

  // Most recent intervals (ms), oldest first.
  size_t Copy(float* out, size_t max) const {
    std::lock_guard lock(mutex_);
    const size_t count = std::min({max, values_.size(), next_});
    for (size_t i = 0; i < count; ++i) {
      out[i] = values_[(next_ - count + i) % values_.size()];
    }
    return count;
  }

  SeriesStats Summarize() const {
    std::vector<float> v(kSummaryFrames);
    v.resize(Copy(v.data(), v.size()));
    SeriesStats s;
    if (v.empty()) {
      return s;
    }
    double sum = 0.0;
    for (float ms : v) {
      sum += ms;
      s.max_ms = std::max(s.max_ms, ms);
    }
    s.avg_ms = float(sum / v.size());
    s.fps = s.avg_ms > 0.0f ? 1000.0f / s.avg_ms : 0.0f;
    std::sort(v.begin(), v.end());
    s.p99_ms = v[std::min(v.size() - 1, v.size() * 99 / 100)];
    return s;
  }

 private:
  mutable std::mutex mutex_;
  std::array<float, kHistoryFrames> values_{};
  size_t next_ = 0;
  int64_t last_ns_ = 0;
};

History g_game;
History g_display;

// ---- Present hook (measurement only) ---------------------------------------

std::atomic<uint32_t> g_game_frames{0};

// With --fps_log, presents and game frames per second every 2 s (every game
// frame should reach a present).
void CountPresent() {
  if (!REXCVAR_GET(fps_log)) {
    return;
  }
  static uint32_t presents = 0;
  static int64_t window_start = 0;
  const int64_t now = NowNs();
  ++presents;
  if (!window_start) {
    window_start = now;
    g_game_frames.store(0, std::memory_order_relaxed);
  } else if (now - window_start >= 2'000'000'000) {
    const double secs = double(now - window_start) * 1e-9;
    const uint32_t frames = g_game_frames.exchange(0, std::memory_order_relaxed);
    REXLOG_INFO("Amped3: presents {:.1f}/s, game frames {:.1f}/s", presents / secs, frames / secs);
    presents = 0;
    window_start = now;
  }
}

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT,
                                               const DXGI_PRESENT_PARAMETERS*);
PresentFn g_original_present = nullptr;
Present1Fn g_original_present1 = nullptr;

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* swap_chain, UINT sync, UINT flags) {
  if (!(flags & DXGI_PRESENT_TEST)) {
    g_display.Record();
    CountPresent();
  }
  return g_original_present(swap_chain, sync, flags);
}

HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* swap_chain, UINT sync, UINT flags,
                                       const DXGI_PRESENT_PARAMETERS* params) {
  if (!(flags & DXGI_PRESENT_TEST)) {
    g_display.Record();
    CountPresent();
  }
  return g_original_present1(swap_chain, sync, flags, params);
}

void PatchSlot(void** slot, void* hook, void** original) {
  DWORD old_protect;
  if (VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
    *original = *slot;
    *slot = hook;
    VirtualProtect(slot, sizeof(void*), old_protect, &old_protect);
  }
}

// ---- Window ------------------------------------------------------------------

constexpr int kWidth = 560;
constexpr int kHeight = 232;
constexpr int kMargin = 16;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kRefreshMs = 100;
constexpr UINT kShowMessage = WM_APP + 1;
constexpr UINT kHideMessage = WM_APP + 2;

std::atomic<HWND> g_window{nullptr};
std::atomic<HWND> g_game_window{nullptr};
std::atomic<bool> g_visible{false};
std::atomic<int32_t> g_target_fps{60};

constexpr COLORREF kGameColor = RGB(120, 140, 200);

COLORREF FpsColor(float fps) {
  return fps >= 115.0f ? RGB(0, 230, 255)
         : fps >= 55.0f ? RGB(60, 230, 110)
         : fps >= 28.0f ? RGB(255, 215, 60)
                        : RGB(255, 80, 80);
}

// Green within 12% of the target interval, yellow within 25%, red beyond.
COLORREF DeviationColor(float ms, float target_ms) {
  const float d = std::abs(ms - target_ms) / target_ms;
  return d <= 0.12f ? RGB(60, 230, 110) : d <= 0.25f ? RGB(255, 210, 60) : RGB(255, 70, 70);
}

void Text(HDC dc, int x, int y, COLORREF color, const char* text) {
  SetTextColor(dc, color);
  TextOutA(dc, x, y, text, int(std::strlen(text)));
}

void Line(HDC dc, int x0, int y0, int x1, int y1, COLORREF color, int width = 1) {
  HPEN pen = CreatePen(PS_SOLID, width, color);
  HGDIOBJ old = SelectObject(dc, pen);
  MoveToEx(dc, x0, y0, nullptr);
  LineTo(dc, x1, y1);
  SelectObject(dc, old);
  DeleteObject(pen);
}

void Fill(HDC dc, RECT rect, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(dc, &rect, brush);
  DeleteObject(brush);
}

void Paint(HDC dc) {
  Fill(dc, {0, 0, kWidth, kHeight}, RGB(14, 18, 28));
  SetBkMode(dc, TRANSPARENT);
  HFONT font = CreateFontA(-14, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
  HGDIOBJ old_font = SelectObject(dc, font);

  const auto display = g_display.Summarize();
  const auto game = g_game.Summarize();
  char line[160];
  std::snprintf(line, sizeof(line), "FPS %5.1f", display.fps);
  Text(dc, 10, 6, FpsColor(display.fps), line);
  std::snprintf(line, sizeof(line), "display avg %6.2f  1%% %6.2f  worst %6.2f ms",
                display.avg_ms, display.p99_ms, display.max_ms);
  Text(dc, 100, 6, RGB(200, 205, 215), line);
  std::snprintf(line, sizeof(line), "game    avg %6.2f  1%% %6.2f  worst %6.2f ms", game.avg_ms,
                game.p99_ms, game.max_ms);
  Text(dc, 100, 24, kGameColor, line);

  // Scale: the target frame interval +/- 50% (unlimited: the measured average).
  const int32_t target_fps = g_target_fps.load();
  float target_ms = target_fps > 0 ? 1000.0f / float(target_fps) : display.avg_ms;
  if (target_ms < 2.0f) {
    target_ms = 16.667f;
  }
  const float min_ms = target_ms * 0.5f, max_ms = target_ms * 1.5f;
  const int gx0 = 10, gy0 = 46, gx1 = kWidth - 56, gy1 = kHeight - 26;
  auto y_of = [&](float ms) {
    const float t = (std::clamp(ms, min_ms, max_ms) - min_ms) / (max_ms - min_ms);
    return int(std::lround(gy1 - t * (gy1 - gy0)));
  };
  Fill(dc, {gx0, gy0, gx1, gy1}, RGB(6, 8, 12));
  Fill(dc, {gx0, y_of(target_ms * 1.12f), gx1, y_of(target_ms * 0.88f)}, RGB(16, 46, 26));
  Line(dc, gx0, y_of(target_ms * 1.25f), gx1, y_of(target_ms * 1.25f), RGB(45, 50, 60));
  Line(dc, gx0, y_of(target_ms * 0.75f), gx1, y_of(target_ms * 0.75f), RGB(45, 50, 60));
  Line(dc, gx0, y_of(target_ms), gx1, y_of(target_ms), RGB(130, 135, 145));
  for (float f : {1.5f, 1.25f, 1.0f, 0.75f, 0.5f}) {
    std::snprintf(line, sizeof(line), "%5.1f", target_ms * f);
    Text(dc, gx1 + 6, y_of(target_ms * f) - 8, RGB(150, 155, 170), line);
  }

  const float step = float(gx1 - gx0) / float(kHistoryFrames - 1);
  float history[kHistoryFrames];
  // Game frames: thin line behind.
  size_t count = g_game.Copy(history, kHistoryFrames);
  for (size_t i = 1; i < count; ++i) {
    const float x = float(gx1) - step * float(count - 1 - i);
    Line(dc, int(x - step), y_of(history[i - 1]), int(x), y_of(history[i]), kGameColor);
  }
  // Display frames: thick, coloured by deviation; off-scale spikes marked.
  count = g_display.Copy(history, kHistoryFrames);
  float worst = 0.0f;
  for (size_t i = 0; i < count; ++i) {
    const float x = float(gx1) - step * float(count - 1 - i);
    worst = std::max(worst, history[i]);
    if (i > 0) {
      Line(dc, int(x - step), y_of(history[i - 1]), int(x), y_of(history[i]),
           DeviationColor(history[i], target_ms), 2);
    }
    if (history[i] > max_ms || history[i] < min_ms) {
      const int y = history[i] > max_ms ? gy0 + 3 : gy1 - 3;
      Fill(dc, {int(x) - 3, y - 3, int(x) + 4, y + 4}, RGB(255, 70, 70));
    }
  }
  if (worst > max_ms) {
    std::snprintf(line, sizeof(line), "spike %.1f ms", worst);
    Text(dc, gx0 + 6, gy0 + 4, RGB(255, 110, 110), line);
  }
  std::snprintf(line, sizeof(line), "last %zu frames  thick = display, thin = game  F10 hides",
                count);
  Text(dc, 10, kHeight - 21, RGB(120, 125, 140), line);

  SelectObject(dc, old_font);
  DeleteObject(font);
}

void UpdatePlacement(HWND hwnd) {
  HWND game = g_game_window.load();
  const bool show = g_visible.load() && game && IsWindowVisible(game) && !IsIconic(game) &&
                    GetForegroundWindow() == game;
  if (!show) {
    ShowWindow(hwnd, SW_HIDE);
    return;
  }
  POINT origin{kMargin, kMargin};
  ClientToScreen(game, &origin);
  SetWindowPos(hwnd, HWND_TOPMOST, origin.x, origin.y, kWidth, kHeight,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_TIMER:
    case kShowMessage:
    case kHideMessage:
      UpdatePlacement(hwnd);
      if (IsWindowVisible(hwnd)) {
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      // Double-buffered to avoid flicker.
      HDC mem = CreateCompatibleDC(dc);
      HBITMAP bitmap = CreateCompatibleBitmap(dc, kWidth, kHeight);
      HGDIOBJ old = SelectObject(mem, bitmap);
      Paint(mem);
      BitBlt(dc, 0, 0, kWidth, kHeight, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
      DeleteObject(bitmap);
      DeleteDC(mem);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_NCHITTEST:
      return HTTRANSPARENT;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

void ThreadMain() {
  WNDCLASSW wc{};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Amped3FrameGraph";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      wc.lpszClassName, L"", WS_POPUP, 0, 0, kWidth, kHeight, nullptr, nullptr, wc.hInstance,
      nullptr);
  if (!hwnd) {
    return;
  }
  SetLayeredWindowAttributes(hwnd, 0, 225, LWA_ALPHA);
  g_window = hwnd;
  SetTimer(hwnd, kTimerId, kRefreshMs, nullptr);
  UpdatePlacement(hwnd);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    DispatchMessageW(&msg);
  }
}

}  // namespace

void InstallPresentHook() {
  if (g_original_present) {
    return;
  }
  // All DXGI flip-model swap chains share one vtable in dxgi.dll, including
  // the D3D12 presenter's. Find it through a throwaway D3D11 swap chain.
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Amped3PresentHook";
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                            nullptr, wc.hInstance, nullptr);
  DXGI_SWAP_CHAIN_DESC desc{};
  desc.BufferCount = 2;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.OutputWindow = hwnd;
  desc.SampleDesc.Count = 1;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  IDXGISwapChain* swap_chain = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                              nullptr, 0, D3D11_SDK_VERSION, &desc, &swap_chain,
                                              &device, nullptr, &context))) {
    void** vtable = *reinterpret_cast<void***>(swap_chain);
    PatchSlot(&vtable[8], reinterpret_cast<void*>(&HookPresent),
              reinterpret_cast<void**>(&g_original_present));
    PatchSlot(&vtable[22], reinterpret_cast<void*>(&HookPresent1),
              reinterpret_cast<void**>(&g_original_present1));
    swap_chain->Release();
    context->Release();
    device->Release();
    REXLOG_INFO("Amped3: present measurement installed");
  } else {
    REXLOG_WARN("Amped3: present measurement unavailable");
  }
  DestroyWindow(hwnd);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

void RecordGameFrame() {
  g_game.Record();
  g_game_frames.fetch_add(1, std::memory_order_relaxed);
}

void Show(void* game_window, int32_t target_fps) {
  g_game_window = static_cast<HWND>(game_window);
  g_target_fps = target_fps;
  g_visible = true;
  if (HWND hwnd = g_window.load()) {
    PostMessageW(hwnd, kShowMessage, 0, 0);
    return;
  }
  static std::atomic<bool> started{false};
  if (!started.exchange(true)) {
    std::thread(ThreadMain).detach();
  }
}

void Hide() {
  g_visible = false;
  if (HWND hwnd = g_window.load()) {
    PostMessageW(hwnd, kHideMessage, 0, 0);
  }
}

bool IsVisible() { return g_visible.load(); }

}  // namespace amped3::frame_graph
