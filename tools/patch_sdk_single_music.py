"""ReXGlue v0.10.0 patch (Amped 3 workaround): --xma_single_music keeps only one music
stream audible.

Amped 3 pauses the menu music when it loads a run or plays a video, starts the
next song, and later resumes the old menu-music cue next to it, so two tracks
play at once (Xenia's new XMA decoder shows the same). The cause is somewhere in
the game's XACT engine (not found yet). A music stream is a stereo context fed
with streaming buffers of 128+ packets (the game's music uses 160). When two
have both been producing output for more than 2 s (longer than a crossfade), the
one that started first is silenced until the game re-initializes its context.
Apply after patch_sdk_audio_levels.py.

usage: patch_sdk_single_music.py <rexglue-src>
"""
import sys
from pathlib import Path

path = Path(sys.argv[1]) / 'src/audio/xma_context.cpp'
src = path.read_text()
if 'xma_single_music' in src:
    print('xma_context.cpp: already patched')
    sys.exit(0)

def rep(old, new):
    global src
    assert src.count(old) == 1, old
    src = src.replace(old, new)

rep('''REXCVAR_DEFINE_STRING(xma_mute, "", "Audio",
                      "Debug: silence the XMA context at this guest address (e.g. FFCA6040)");
''', '''REXCVAR_DEFINE_STRING(xma_mute, "", "Audio",
                      "Debug: silence the XMA context at this guest address (e.g. FFCA6040)");
REXCVAR_DEFINE_BOOL(xma_single_music, false, "Audio",
                    "Keep one music stream audible: after 2 s of two playing together, "
                    "silence the older (Amped 3 resumes paused menu music next to the next song)");
''')
rep('''namespace rex::audio {
// Debug (--audio_levels): one line per decoded chunk or mixed frame.''', '''namespace rex::audio {
namespace {
// --xma_single_music state per context id.
struct MusicStream {
  std::chrono::steady_clock::time_point started{}, last_output{}, overlap_since{};
  bool superseded = false;
};
std::mutex music_mutex;
std::unordered_map<uint32_t, MusicStream> music_streams;

void ForgetMusicStream(uint32_t id) {
  std::lock_guard lock(music_mutex);
  music_streams.erase(id);
}

// Called for each decoded chunk; true when this context's output should be silent.
bool SupersededMusic(uint32_t id, const XMA_CONTEXT_DATA& data) {
  const bool is_music =
      data.is_stereo &&
      std::max(data.input_buffer_0_packet_count, data.input_buffer_1_packet_count) >= 128;
  std::lock_guard lock(music_mutex);
  if (!is_music) {
    music_streams.erase(id);
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  auto [it, inserted] = music_streams.try_emplace(id);
  MusicStream& self = it->second;
  if (inserted) {
    self.started = now;
  }
  self.last_output = now;
  if (self.superseded) {
    return true;
  }
  bool overlapping = false, older = false;
  for (const auto& [other_id, other] : music_streams) {
    if (other_id != id && now - other.last_output < std::chrono::milliseconds(300)) {
      overlapping = true;
      older |= self.started < other.started;
    }
  }
  if (!overlapping) {
    self.overlap_since = {};
    return false;
  }
  if (self.overlap_since == std::chrono::steady_clock::time_point{}) {
    self.overlap_since = now;
  }
  if (older && now - self.overlap_since > std::chrono::seconds(2)) {
    self.superseded = true;
    REXAPU_INFO("XmaContext {:08X}: music superseded by a newer stream; silenced", id);
    return true;
  }
  return false;
}
}  // namespace

// Debug (--audio_levels): one line per decoded chunk or mixed frame.''')
rep('''  const bool muted = !REXCVAR_GET(xma_mute).empty() && REXCVAR_GET(xma_mute) == context_name;''',
    '''  const bool muted = (!REXCVAR_GET(xma_mute).empty() && REXCVAR_GET(xma_mute) == context_name) ||
                     (REXCVAR_GET(xma_single_music) && SupersededMusic(id(), *data));''')
rep('''void XmaContext::Clear() {
  std::lock_guard<std::mutex> lock(lock_);''', '''void XmaContext::Clear() {
  ForgetMusicStream(id());  // --xma_single_music: a new stream starts here.
  std::lock_guard<std::mutex> lock(lock_);''')
rep('#include <mutex>\n', '#include <mutex>\n#include <unordered_map>\n')
rep('#include <rex/audio/xma/context.h>\n#include <rex/cvar.h>\n', '#include <rex/audio/xma/context.h>\n#include <rex/cvar.h>\n#include <rex/logging.h>\n')
path.write_text(src)
print('xma_context.cpp: patched')
