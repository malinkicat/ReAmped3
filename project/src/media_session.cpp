// media_session.cpp - Another music player (e.g. Spotify) in place of the
// game's soundtrack (--user_music=system)
//
// Windows: the system media session (what the volume flyout shows) every half
// second: while a player is playing, the runtime silences the game's music and
// the now-playing popup shows its track; the game's song change (D-pad) skips
// in that player. Under Proton (Steam Deck, Linux) the game can't see Linux
// players: run.sh passes the playing MPRIS player on in media_session.txt
// ("playing" or "stopped", then "Artist - Title") and carries out
// media_command.txt ("next" / "previous").

#include "media_session.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <optional>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

void Amped3_RequestGameSongChange(int delta);  // save_hooks.cpp

REXCVAR_DEFINE_INT32(turn_min_seconds, 15, "Audio",
                     "Taking turns with another player: a track that changes sooner than this was "
                     "skipped (still the player's turn)");

namespace amped3::media_session {

namespace {
std::mutex g_player_mutex;
std::string g_player_app;  // the followed player's app id (Windows)
}  // namespace

std::string PlayerName() {
  std::string app;
  {
    std::lock_guard lock(g_player_mutex);
    app = g_player_app;
  }
  std::string lower = app;
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  // Under Proton the bridge follows Spotify first (run.sh).
  return lower.find("spotify") != std::string::npos || app.empty() ? "Spotify" : "Music player";
}
namespace {

using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

std::atomic<int32_t> g_skip{0};  // pending skips: + next, - previous (one per press)
// Taking turns with the player (mix mode, music folder empty): the game's
// song plays now (the player paused by us); ended early by "next".
std::atomic<bool> g_game_turn{false}, g_end_game_turn{false};

template <typename T>
T RuntimeFn(const char* name) {
  HMODULE runtime = GetModuleHandleA("rexruntime.dll");
  return runtime ? reinterpret_cast<T>(GetProcAddress(runtime, name)) : nullptr;
}

// Mix mode with nothing in the music folder: the player takes turns with the
// game's songs.
bool TurnsMode() {
  if (rex::cvar::GetFlagByName("user_music") != "mix") return false;
  static const auto count = RuntimeFn<int32_t (*)()>("ReXUserMusicTrackCount");
  return count && count() == 0;
}

// The player is the music source: "system", or turns.
bool PlayerIsSource() { return rex::cvar::GetFlagByName("user_music") == "system" || TurnsMode(); }

uint32_t TitleSongStarts() {
  static const auto starts = RuntimeFn<uint32_t (*)()>("ReXTitleSongStarts");
  return starts ? starts() : 0;
}

// Spotify isn't running: start it minimized, without taking focus (once).
bool LaunchSpotify() {
  wchar_t appdata[MAX_PATH];
  if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) return false;
  const std::wstring exe = std::wstring(appdata) + L"\\Spotify\\Spotify.exe";
  if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
  const auto result = reinterpret_cast<intptr_t>(
      ShellExecuteW(nullptr, L"open", exe.c_str(), L"--minimized", nullptr, SW_SHOWMINNOACTIVE));
  return result > 32;
}
std::once_flag g_started;

// Spotify.exe is running (whether or not Windows lists it as a media player).
bool SpotifyRunning() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  for (BOOL ok = Process32FirstW(snap, &entry); ok && !found; ok = Process32NextW(snap, &entry)) {
    found = _wcsicmp(entry.szExeFile, L"Spotify.exe") == 0;
  }
  CloseHandle(snap);
  return found;
}

// The media Play/Pause key: a Spotify that hasn't played since it opened has
// no media session to press play on, but it answers the key.
void PressMediaPlay() {
  keybd_event(VK_MEDIA_PLAY_PAUSE, 0, KEYEVENTF_EXTENDEDKEY, 0);
  keybd_event(VK_MEDIA_PLAY_PAUSE, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
}

void ReportToRuntime(bool playing, const std::string& text) {
  static const auto report = [] {
    HMODULE runtime = GetModuleHandleA("rexruntime.dll");
    return runtime ? reinterpret_cast<void (*)(int32_t, const char*)>(
                         GetProcAddress(runtime, "ReXSetExternalMusic"))
                   : nullptr;
  }();
  if (report) report(playing ? 1 : 0, text.c_str());
}

// Whether the game has taken music over (its cutscenes and scripted songs,
// XMPSetPlaybackController): the other player is paused meanwhile, as the
// Xbox 360 did with a custom soundtrack, and resumed afterwards (only if we
// paused it).
bool TitleOwnsMusic() {
  static const auto owns = [] {
    HMODULE runtime = GetModuleHandleA("rexruntime.dll");
    return runtime ? reinterpret_cast<int32_t (*)()>(GetProcAddress(runtime, "ReXTitleOwnsMusic")) : nullptr;
  }();
  return owns && owns() != 0;
}

// A player stopping is believed only after it has stayed stopped for a
// while: between tracks, while seeking, or when Windows changes its current
// session (alt-tab) a player reports a moment of not playing, and each flip
// would hand the music back to the game (its own song and song popup) and
// take it away again.
class Settled {
 public:
  bool Update(bool playing) {
    const auto now = std::chrono::steady_clock::now();
    if (playing) last_playing_ = now;
    reported_ = playing || (reported_ && now - last_playing_ < std::chrono::milliseconds(2500));
    return reported_;
  }

 private:
  bool reported_ = false;
  std::chrono::steady_clock::time_point last_playing_{};
};

// "Your music volume" (user_music_volume, 0-100), set on the other player.
// Only after it changes in game: until then the player keeps its own level.
int MusicVolume() {
  const std::string v = rex::cvar::GetFlagByName("user_music_volume");
  return v.empty() ? -1 : std::clamp(std::atoi(v.c_str()), 0, 100);
}

// Windows: the volume mixer's level for the app `app` (a session's app id,
// "Spotify.exe" for desktop apps).
void SetAppVolume(const winrt::hstring& app, int volume) {
  using Microsoft::WRL::ComPtr;
  std::wstring want(app.c_str());
  if (want.empty() || want.find(L'!') != std::wstring::npos) return;  // a store app: not by exe name
  ComPtr<IMMDeviceEnumerator> devices;
  ComPtr<IMMDevice> device;
  ComPtr<IAudioSessionManager2> manager;
  ComPtr<IAudioSessionEnumerator> sessions;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) ||
      FAILED(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) ||
      FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager)) ||
      FAILED(manager->GetSessionEnumerator(&sessions))) {
    return;
  }
  int count = 0, set = 0;
  sessions->GetCount(&count);
  for (int i = 0; i < count; ++i) {
    ComPtr<IAudioSessionControl> control;
    ComPtr<IAudioSessionControl2> control2;
    ComPtr<ISimpleAudioVolume> level;
    DWORD pid = 0;
    if (FAILED(sessions->GetSession(i, &control)) || FAILED(control.As(&control2)) ||
        FAILED(control2->GetProcessId(&pid)) || !pid) {
      continue;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) continue;
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    const bool named = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!named) continue;
    const wchar_t* name = wcsrchr(path, L'\\');
    if (_wcsicmp(name ? name + 1 : path, want.c_str()) != 0) continue;
    if (SUCCEEDED(control.As(&level)) && SUCCEEDED(level->SetMasterVolume(volume / 100.0f, nullptr))) ++set;
  }
  REXLOG_INFO("Amped3: media session: {} volume {}% ({} sessions)", winrt::to_string(app), volume, set);
}

bool UnderWine() {
  HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  return ntdll && GetProcAddress(ntdll, "wine_get_version");
}

// Proton: run.sh's MPRIS bridge.
void RunFromFile() {
  const auto folder = rex::filesystem::GetExecutableFolder();
  REXLOG_INFO("Amped3: media session: reading media_session.txt (Linux players via run.sh)");
  {
    std::error_code ec;  // an earlier session's commands aren't carried out now
    std::filesystem::remove(folder / "media_command.txt", ec);
  }
  bool was_playing = false, paused_by_us = false;
  Settled settled;
  std::string last_text;
  auto command = [&](const char* what) {
    // Appended (one a line, LF only): ReAmped3.sh takes the file and carries
    // them out in order, so quick ones in a row aren't lost.
    std::ofstream out(folder / "media_command.txt", std::ios::binary | std::ios::app);
    out << what << "\n";
    if (!out) REXLOG_WARN("Amped3: media session: couldn't write media_command.txt");
  };
  int volume = MusicVolume();
  const auto started = std::chrono::steady_clock::now();
  bool launch_paused = false;
  // The player starts by itself in game, as on Windows: once, as soon as
  // run.sh reports it not playing (again after choosing it again).
  bool autoplayed = false;
  for (;;) {
    if (!PlayerIsSource()) {
      autoplayed = false;
      // At launch with the game's music chosen: a player already playing is
      // paused once (run.sh's first reports come a few seconds in).
      if (!launch_paused && std::chrono::steady_clock::now() - started < std::chrono::seconds(15)) {
        std::string status;
        if (std::ifstream in(folder / "media_session.txt"); in) std::getline(in, status);
        if (status == "playing") {
          launch_paused = true;
          command("pause");
          REXLOG_INFO("Amped3: media session: paused the player at launch (the game's music is chosen)");
        }
      }
      // Another source chosen: the player's music isn't the game's any more,
      // and the player is paused.
      if (was_playing) {
        command("pause");
        paused_by_us = false;
        ReportToRuntime(false, "");
        was_playing = false;
        REXLOG_INFO("Amped3: media session: stopped (another music source)");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }
    for (int32_t skip = g_skip.exchange(0); skip; skip -= skip > 0 ? 1 : -1) command(skip > 0 ? "next" : "previous");
    if (const int v = MusicVolume(); v != volume && v >= 0) {
      volume = v;
      command(("volume " + std::to_string(v)).c_str());
    }
    const bool title = TitleOwnsMusic();
    if (title && was_playing && !paused_by_us) {
      command("pause");
      paused_by_us = true;
      REXLOG_INFO("Amped3: media session: paused for the game's music");
    } else if (!title && paused_by_us) {
      command("play");
      paused_by_us = false;
      REXLOG_INFO("Amped3: media session: resumed");
    }
    std::string status, text;
    if (std::ifstream in(folder / "media_session.txt"); in) {
      std::getline(in, status);
      std::getline(in, text);
    }
    // Paused by us for the game's music: still the player's music (the
    // runtime knows the title has it meanwhile).
    const bool playing = settled.Update(status == "playing" || paused_by_us);
    if (playing != was_playing) {
      REXLOG_INFO("Amped3: media session: {}", playing ? "playing: " + text : "stopped");
      was_playing = playing;
    }
    if (status == "playing") last_text = text;
    ReportToRuntime(playing, last_text);
    if (!autoplayed && !title && !paused_by_us && status == "stopped") {
      autoplayed = true;
      command("play");
      REXLOG_INFO("Amped3: media session: started the player");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

void Run() {
  if (UnderWine()) {
    RunFromFile();
    return;
  }
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    REXLOG_INFO("Amped3: media session: watching the system's media players");
    bool was_playing = false;
    Settled settled;
    std::string last_text;
    using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
    // The player we follow (its app id): kept while it plays, whichever
    // session Windows calls current (that changes with focus).
    winrt::hstring followed;
    int volume = MusicVolume();
    // The player we paused for the game's music (resumed afterwards).
    std::optional<Session> paused_by_us;
    auto is_playing = [](const Session& session) {
      return session.GetPlaybackInfo().PlaybackStatus() ==
             GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
    };
    bool autoplayed = false, launched = false;
    auto no_player_since = std::chrono::steady_clock::now();
    std::string turn_text;                       // the track the player's turn began with
    auto turn_start = std::chrono::steady_clock::now();
    uint32_t game_turn_starts = 0;               // the game's song starts counted from
    auto game_turn_since = std::chrono::steady_clock::now();
    bool launch_checked = false;
    for (;;) {
      if (!PlayerIsSource()) {
        // At launch with the game's soundtrack (or your files) chosen: a player
        // already playing is paused, once, so the game's music takes over.
        // (Not again after that: playing one by hand mid-game is yours.)
        if (!launch_checked) {
          launch_checked = true;
          for (const auto& session : manager.GetSessions()) {
            if (is_playing(session)) {
              session.TryPauseAsync().get();
              REXLOG_INFO("Amped3: media session: paused {} at launch (the game's music is chosen)",
                          winrt::to_string(session.SourceAppUserModelId()));
            }
          }
        }
        // Another source chosen: the player's music isn't the game's any more,
        // and the player is paused (it would play on over the game's music).
        if (was_playing) {
          for (const auto& session : manager.GetSessions()) {
            if (session.SourceAppUserModelId() == followed && is_playing(session)) session.TryPauseAsync().get();
          }
          paused_by_us.reset();
          ReportToRuntime(false, "");
          was_playing = false;
          autoplayed = false;  // choosing the player again starts it again
          REXLOG_INFO("Amped3: media session: paused (another music source)");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      }
      launch_checked = true;
      const bool turns = TurnsMode();
      if (!turns && g_game_turn) g_game_turn = false;
      const bool title = TitleOwnsMusic();
      if (!title && paused_by_us) {
        paused_by_us->TryPlayAsync().get();
        paused_by_us.reset();
        turn_text.clear();  // its turn starts again from here
        REXLOG_INFO("Amped3: media session: resumed");
      }
      // The followed player if it still plays, else any playing one, else the
      // followed one (paused), else Windows' current session.
      std::optional<Session> chosen, playing_one, followed_one;
      for (const auto& session : manager.GetSessions()) {
        const bool on = is_playing(session);
        if (session.SourceAppUserModelId() == followed) {
          followed_one = session;
          if (on) chosen = session;
        }
        if (on && !playing_one) playing_one = session;
      }
      if (!chosen) chosen = playing_one;
      if (!chosen) chosen = followed_one;
      if (!chosen) {
        if (auto current = manager.GetCurrentSession()) chosen = current;
      }
      if (!chosen && !launched) {
        launched = true;
        if (LaunchSpotify()) REXLOG_INFO("Amped3: media session: starting Spotify (minimized)");
        no_player_since = std::chrono::steady_clock::now();
      }
      // No media player at all but Spotify open (it only becomes one once it
      // plays): the media key starts it, once, after it's had time to open.
      // Only with no other player, so the key can't go to anything else.
      if (!chosen && !autoplayed && !title && !g_game_turn &&
          std::chrono::steady_clock::now() - no_player_since > std::chrono::seconds(4) && SpotifyRunning()) {
        autoplayed = true;
        PressMediaPlay();
        REXLOG_INFO("Amped3: media session: started Spotify with the media key (no media session yet)");
      }
      if (!chosen) g_skip.store(0);  // nothing to skip in (no player at all)
      bool playing = false;
      if (chosen) {
        if (chosen->SourceAppUserModelId() != followed) {
          followed = chosen->SourceAppUserModelId();
          {
            std::lock_guard lock(g_player_mutex);
            g_player_app = winrt::to_string(followed);
          }
          REXLOG_INFO("Amped3: media session: following {}", winrt::to_string(followed));
        }
        if (const int v = MusicVolume(); v != volume && v >= 0) {
          volume = v;
          SetAppVolume(followed, v);
        }
        for (int32_t skip = g_skip.exchange(0); skip; skip -= skip > 0 ? 1 : -1) {
          if (skip > 0) chosen->TrySkipNextAsync().get();
          else chosen->TrySkipPreviousAsync().get();
        }
        if (title && !paused_by_us && is_playing(*chosen)) {
          chosen->TryPauseAsync().get();
          paused_by_us = *chosen;
          REXLOG_INFO("Amped3: media session: paused for the game's music");
        }
        // The player starts by itself: no switching to it to press play.
        if (!autoplayed && !title && !g_game_turn && !is_playing(*chosen)) {
          autoplayed = true;
          chosen->TryPlayAsync().get();
          REXLOG_INFO("Amped3: media session: started the player");
        }
        playing = is_playing(*chosen);
        const auto now = std::chrono::steady_clock::now();
        if (turns && g_game_turn) {
          // The game's turn: one of its songs. Over when its next song
          // starts (10 s in: the one asked for has started by then), on
          // "next", or after 5 minutes.
          if (now - game_turn_since < std::chrono::seconds(10)) game_turn_starts = TitleSongStarts();
          if (g_end_game_turn.exchange(false) || TitleSongStarts() != game_turn_starts ||
              now - game_turn_since > std::chrono::minutes(5)) {
            g_game_turn = false;
            chosen->TryPlayAsync().get();
            turn_text.clear();
            REXLOG_INFO("Amped3: media session: the player's turn");
          }
        } else if (turns && playing) {
          const auto props = chosen->TryGetMediaPropertiesAsync().get();
          const std::string now_text = winrt::to_string(props.Artist()) + " - " + winrt::to_string(props.Title());
          if (turn_text.empty()) {
            turn_text = now_text;
            turn_start = now;
          } else if (now_text != turn_text) {
            if (now - turn_start > std::chrono::seconds(REXCVAR_GET(turn_min_seconds))) {
              // A track played out: the game's song next (the player
              // paused on its next track, resumed after).
              chosen->TryPauseAsync().get();
              g_game_turn = true;
              game_turn_since = now;
              game_turn_starts = TitleSongStarts();
              ::Amped3_RequestGameSongChange(1);
              playing = false;
              REXLOG_INFO("Amped3: media session: the game's turn");
            } else {
              turn_text = now_text;  // skipped by the player: still its turn
              turn_start = now;
            }
          }
        }
        if (playing) {
          const auto props = chosen->TryGetMediaPropertiesAsync().get();
          const std::string artist = winrt::to_string(props.Artist());
          const std::string song = winrt::to_string(props.Title());
          last_text = artist.empty() ? song : artist + " - " + song;
        }
      }
      // Paused by us for the game's music: still the player's music (the
      // runtime knows the title has it meanwhile).
      const bool reported = settled.Update(playing || paused_by_us.has_value());
      if (reported != was_playing) {
        REXLOG_INFO("Amped3: media session: {}", reported ? "playing: " + last_text : "stopped");
        was_playing = reported;
      }
      ReportToRuntime(reported, last_text);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  } catch (const winrt::hresult_error& e) {
    REXLOG_WARN("Amped3: media session: not available ({:08X})", uint32_t(e.code().value));
  } catch (...) {
    REXLOG_WARN("Amped3: media session: not available");
  }
  ReportToRuntime(false, "");
}

}  // namespace

void OnQuit() {
  // The player was the game's music: it stops with the game (paused). Once
  // (the quit prompt and the window closing both get here).
  static std::atomic<bool> done{false};
  if (!PlayerIsSource() || done.exchange(true)) return;
  if (UnderWine()) {
    // ReAmped3.sh pauses it once the game has gone.
    std::ofstream out(rex::filesystem::GetExecutableFolder() / "media_command.txt", std::ios::binary | std::ios::app);
    out << "pause\n";
    REXLOG_INFO("Amped3: media session: pausing the player (quit)");
    return;
  }
  std::string app;
  {
    std::lock_guard lock(g_player_mutex);
    app = g_player_app;
  }
  if (app.empty()) return;
  // Its own thread: WinRT's blocking get() needs a multi-threaded apartment.
  std::thread([app] {
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
      const auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
      for (const auto& session : manager.GetSessions()) {
        if (winrt::to_string(session.SourceAppUserModelId()) == app &&
            session.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
          session.TryPauseAsync().get();
          REXLOG_INFO("Amped3: media session: paused {} (quit)", app);
        }
      }
    } catch (const winrt::hresult_error& e) {
      REXLOG_WARN("Amped3: media session: pause at quit failed ({:#x})", static_cast<uint32_t>(e.code()));
    }
  }).join();
}

void OnFrame() {
  // Whatever the source: with the game's music chosen it pauses a player
  // already playing at launch, and it follows a switch to the player.
  std::call_once(g_started, [] { std::thread(Run).detach(); });
}

bool Active() { return PlayerIsSource(); }
bool InGameTurn() { return g_game_turn.load(); }
void EndGameTurn() {
  REXLOG_INFO("Amped3: media session: next (the game's turn ends)");
  g_end_game_turn = true;
}

void Skip(int32_t delta) {
  // The game calls its song change once per press (a held D-pad doesn't
  // repeat, checked on the Deck), so quick presses all count: they add up
  // until the player is told (at most 5 either way).
  REXLOG_INFO("Amped3: media session: {} track", delta < 0 ? "previous" : "next");
  int32_t pending = g_skip.load();
  const int32_t step = delta < 0 ? -1 : 1;
  while (std::abs(pending + step) <= 5 && !g_skip.compare_exchange_weak(pending, pending + step)) {
  }
}

}  // namespace amped3::media_session
