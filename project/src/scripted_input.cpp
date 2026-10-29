// scripted_input.cpp - Test-only virtual controller driven by a command file

#include "scripted_input.h"

#include "cpu_profile.h"

namespace amped3::rider_probe {
void RequestDump();
}
#include "pix_capture.h"

#include <rex/chrono/clock.h>
#include <rex/filesystem.h>
#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/logging.h>

#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace amped3::scripted_input {
namespace {

using Clock = std::chrono::steady_clock;
using rex::X_RESULT;
using rex::X_STATUS;
using rex::input::DeviceId;
using rex::input::DeviceInfo;

constexpr DeviceId kDevice = static_cast<DeviceId>(0x5C21);
constexpr auto kReleaseGap = std::chrono::milliseconds(120);
constexpr auto kPollInterval = std::chrono::milliseconds(100);

struct Step {
  uint16_t buttons = 0;  // 0 = wait
  std::chrono::milliseconds duration{0};
};

const std::unordered_map<std::string, uint16_t> kButtons = {
    {"up", rex::input::X_INPUT_GAMEPAD_DPAD_UP},
    {"down", rex::input::X_INPUT_GAMEPAD_DPAD_DOWN},
    {"left", rex::input::X_INPUT_GAMEPAD_DPAD_LEFT},
    {"right", rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT},
    {"start", rex::input::X_INPUT_GAMEPAD_START},
    {"back", rex::input::X_INPUT_GAMEPAD_BACK},
    {"lb", rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER},
    {"rb", rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER},
    {"a", rex::input::X_INPUT_GAMEPAD_A},
    {"b", rex::input::X_INPUT_GAMEPAD_B},
    {"x", rex::input::X_INPUT_GAMEPAD_X},
    {"y", rex::input::X_INPUT_GAMEPAD_Y},
};

// XInput keystroke virtual keys (VK_PAD_*) for each button; the game reads some
// presses (e.g. skipping the story) as keystrokes rather than pad state.
const std::pair<uint16_t, uint16_t> kPadKeys[] = {
    {rex::input::X_INPUT_GAMEPAD_A, 0x5800},          {rex::input::X_INPUT_GAMEPAD_B, 0x5801},
    {rex::input::X_INPUT_GAMEPAD_X, 0x5802},          {rex::input::X_INPUT_GAMEPAD_Y, 0x5803},
    {rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER, 0x5804},
    {rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER, 0x5805},
    {rex::input::X_INPUT_GAMEPAD_DPAD_UP, 0x5810},    {rex::input::X_INPUT_GAMEPAD_DPAD_DOWN, 0x5811},
    {rex::input::X_INPUT_GAMEPAD_DPAD_LEFT, 0x5812},  {rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT, 0x5813},
    {rex::input::X_INPUT_GAMEPAD_START, 0x5814},      {rex::input::X_INPUT_GAMEPAD_BACK, 0x5815},
};
constexpr uint16_t kKeyDown = 0x0001, kKeyUp = 0x0002;

class ScriptedInputDriver final : public rex::input::InputDriver {
 public:
  ScriptedInputDriver()
      : InputDriver(nullptr, 0),
        script_path_(rex::filesystem::GetExecutableFolder() / "input_script.txt") {}

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    DeviceInfo info;
    info.id = kDevice;
    info.name = "Amped3 scripted pad";
    info.guid = "amped3-scripted";
    info.synthetic = true;
    out.push_back(info);
  }

  X_RESULT GetDeviceState(DeviceId id, rex::input::X_INPUT_STATE* out_state) override {
    if (id != kDevice) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    if (!polled_) {
      polled_ = true;
      REXLOG_INFO("Amped3: scripted input polled; watching {}", script_path_.string());
    }
    // Like a real pad, report nothing while input is inactive (e.g. a system dialog is up).
    const uint16_t buttons = is_active() ? Update() : 0;
    if (out_state) {
      std::memset(out_state, 0, sizeof(*out_state));
      out_state->packet_number = packet_number_;
      out_state->gamepad.buttons = buttons;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t,
                                 rex::input::X_INPUT_CAPABILITIES* out_caps) override {
    if (id != kDevice) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    if (out_caps) {
      std::memset(out_caps, 0, sizeof(*out_caps));
      out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
      out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
      out_caps->gamepad.buttons = 0xF3FF;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(DeviceId id, rex::input::X_INPUT_VIBRATION*) override {
    return id == kDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t,
                              rex::input::X_INPUT_KEYSTROKE* out_keystroke) override {
    if (id != kDevice) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    if (!is_active()) {
      return X_ERROR_EMPTY;
    }
    Update();  // The script also advances when the game only reads keystrokes.
    std::lock_guard lock(mutex_);
    if (keystrokes_.empty()) {
      return X_ERROR_EMPTY;
    }
    if (out_keystroke) {
      std::memset(out_keystroke, 0, sizeof(*out_keystroke));
      out_keystroke->virtual_key = keystrokes_.front().first;
      out_keystroke->flags = keystrokes_.front().second;
    }
    keystrokes_.pop_front();
    return X_ERROR_SUCCESS;
  }

 private:
  // Advances the script and returns the buttons held right now.
  uint16_t Update() {
    std::lock_guard lock(mutex_);
    const auto now = Clock::now();
    if (steps_.empty() && now >= next_poll_) {
      next_poll_ = now + kPollInterval;
      LoadScript();
    }
    while (!steps_.empty() && now >= step_end_) {
      if (step_started_) {
        steps_.pop_front();
        step_started_ = false;
        continue;
      }
      step_started_ = true;
      step_end_ = now + steps_.front().duration;
      ++packet_number_;
    }
    const uint16_t buttons = step_started_ && !steps_.empty() ? steps_.front().buttons : 0;
    // Queue key down/up events for buttons that changed since the last update.
    for (const auto& [button, key] : kPadKeys) {
      if (((buttons ^ last_buttons_) & button) && keystrokes_.size() < 64) {
        keystrokes_.push_back({key, (buttons & button) ? kKeyDown : kKeyUp});
      }
    }
    last_buttons_ = buttons;
    return buttons;
  }

  void LoadScript() {
    std::error_code ec;
    if (!std::filesystem::exists(script_path_, ec)) {
      return;
    }
    std::ifstream in(script_path_);
    std::string line;
    while (std::getline(in, line)) {
      std::istringstream words(line);
      std::string name;
      int ms = -1;
      if (!(words >> name) || name[0] == '#') {
        continue;
      }
      if (name == "speed") {
        // Test-only: scale guest time (e.g. "speed 8" through the story, then
        // "speed 1"). Applies when the script is loaded.
        double scalar = 1.0;
        words >> scalar;
        rex::chrono::Clock::set_guest_time_scalar(scalar);
        REXLOG_INFO("Amped3: scripted input: guest time x{}", scalar);
        continue;
      }
      if (name == "pixcapture") {
        // Debug: PIX GPU capture of the next N frames (pix_capture.cpp).
        int frames = 1;
        words >> frames;
        amped3::pix::CaptureNextFrames(frames);
        continue;
      }
      if (name == "riderdump") {
        // Debug: dump --rider_dump frames of rider state (rider_probe.cpp).
        amped3::rider_probe::RequestDump();
        continue;
      }
      if (name == "profile") {
        // Test-only: sample host threads for N seconds (cpu_profile.cpp).
        int seconds = 20;
        words >> seconds;
        amped3::cpu_profile::Start(seconds);
        continue;
      }
      words >> ms;
      if (name == "wait") {
        steps_.push_back({0, std::chrono::milliseconds(ms < 0 ? 500 : ms)});
      } else if (auto it = kButtons.find(name); it != kButtons.end()) {
        steps_.push_back({it->second, std::chrono::milliseconds(ms < 0 ? 150 : ms)});
        steps_.push_back({0, kReleaseGap});
      } else {
        REXLOG_WARN("Amped3: scripted input: unknown command '{}'", name);
      }
    }
    in.close();
    std::filesystem::remove(script_path_, ec);
    REXLOG_INFO("Amped3: scripted input loaded {} steps", steps_.size());
  }

  std::filesystem::path script_path_;
  std::mutex mutex_;
  std::deque<Step> steps_;
  bool step_started_ = false;
  Clock::time_point step_end_{};
  Clock::time_point next_poll_{};
  uint32_t packet_number_ = 1;
  uint16_t last_buttons_ = 0;
  std::deque<std::pair<uint16_t, uint16_t>> keystrokes_;  // virtual key, flags
  bool polled_ = false;
};

}  // namespace

std::unique_ptr<rex::input::InputDriver> CreateDriver() {
  return std::make_unique<ScriptedInputDriver>();
}

}  // namespace amped3::scripted_input
