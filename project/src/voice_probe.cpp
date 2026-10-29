// voice_probe.cpp - Debug probe: calls into XAudio2's source-voice methods
//
// With --voice_probe=true, each XAudio2 public API wrapper (0x82747B08-0x827482C0,
// which forward to the internal voice classes whose vtables are at 0x82033A84
// and 0x82033B6C) logs once a second, per object: calls/s, the last r4/r5/f1
// and the caller (lr) - the game's audio engine. When r4 looks
// like an XAUDIO2_BUFFER, its audio data address is logged too, which links a
// voice to a music stream (F8F3xxxx menu music, F8E9xxxx song).
// Hooks are [[midasm_hook]]s on the method entries.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

#include <chrono>
#include <cstring>
#include <cmath>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <tuple>

REXCVAR_DECLARE(bool, voice_probe);

namespace {

struct Calls {
  uint64_t n = 0;
  uint32_t r4 = 0, r5 = 0, lr = 0, data = 0;
  double f1 = 0;
};
std::mutex g_mutex;
std::map<std::pair<std::string, uint32_t>, Calls> g_calls;
auto g_window = std::chrono::steady_clock::now();

uint32_t Load32(uint32_t address) {
  if (address < 0x40000000 || address >= 0xA0000000 || (address & 3)) return 0;
  // Only read mapped, readable guest pages (hooked methods see foreign objects).
  auto* memory = REX_KERNEL_MEMORY();
  auto* heap = const_cast<rex::memory::BaseHeap*>(memory->LookupHeap(address));
  uint32_t protect = 0;
  if (!heap || !heap->QueryProtect(address, &protect) || !(protect & rex::memory::kMemoryProtectRead)) {
    return 0;
  }
  const auto* p = memory->TranslateVirtual<const uint8_t*>(address);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void Record(const char* name, uint32_t self, uint32_t r4, uint32_t r5, double f1) {
  if (!REXCVAR_GET(voice_probe)) return;
  uint32_t lr = 0;
  if (auto* t = rex::system::XThread::GetCurrentThread(); t && t->thread_state() && t->thread_state()->context()) {
    lr = uint32_t(t->thread_state()->context()->lr);
  }
  // XAUDIO2_BUFFER: Flags, AudioBytes, pAudioData, ...
  const uint32_t data = Load32(r4 + 8);
  std::lock_guard lock(g_mutex);
  auto& c = g_calls[{name, self}];
  ++c.n; c.r4 = r4; c.r5 = r5; c.f1 = f1; c.lr = lr;
  if ((data & 0xFF000000) == 0xF8000000) c.data = data;
  const auto now = std::chrono::steady_clock::now();
  if (now - g_window < std::chrono::seconds(1)) return;
  std::string out;
  for (const auto& [key, v] : g_calls) {
    out += fmt::format(" [{} this={:08X} n={} r4={:08X} r5={:08X} f1={:.3f} lr={:08X}{}]", key.first,
                       key.second, v.n, v.r4, v.r5, v.f1, v.lr,
                       v.data ? fmt::format(" data={:08X}", v.data) : "");
  }
  REXLOG_INFO("VoiceProbe:{}", out);
  g_calls.clear();
  g_window = now;
}

// The XMA context a voice object points to: the offset of the context pointer is
// found once by scanning a voice for a pointer into the context array
// (0xFFCA6000-0xFFCA7FFF); voices are pooled, so it is re-read on every use.
int32_t g_context_offset = -1;
uint32_t VoiceContext(uint32_t self) {
  if (g_context_offset < 0) {
    for (uint32_t off = 0; off < 0x1000; off += 4) {
      const uint32_t v = Load32(self + off);
      if (v >= 0xFFCA6000 && v < 0xFFCA8000 && (v & 0x3F) == 0) {
        g_context_offset = int32_t(off);
        REXLOG_INFO("VoiceVolume: XMA context pointer at voice+{:X}", off);
        break;
      }
    }
    if (g_context_offset < 0) return 0;
  }
  return Load32(self + uint32_t(g_context_offset));
}

}  // namespace

// [[midasm_hook]] on the SetVolume wrapper (P82748010: this, f1 = volume):
// one line per change of a voice's volume, with its XMA context.
void Amped3_VoiceVolume(uint32_t self, double volume) {
  static std::map<std::pair<uint32_t, uint32_t>, double> last;
  std::lock_guard lock(g_mutex);
  const uint32_t context = VoiceContext(self);
  auto [it, inserted] = last.try_emplace({self, context}, -1.0);
  if (std::abs(it->second - volume) < 0.02 && volume != 0.0) return;
  if (it->second == volume) return;
  it->second = volume;
  REXLOG_INFO("VoiceVolume: voice={:08X} ctx={:08X} volume={:.3f}", self, context, volume);
}

void Amped3_Voice_82747B08(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747B08", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82747B70(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747B70", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82747BE0(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747BE0", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82747C38(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747C38", r3.u32, r4.u32, r5.u32, f1.f64); }
thread_local uint32_t g_state_voice = 0, g_state_ptr = 0;
void Amped3_Voice_82747C90(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) {
  Record("P82747C90", r3.u32, r4.u32, r5.u32, f1.f64);
  g_state_voice = r3.u32;  // status-flags query: r4 receives a flags byte
  g_state_ptr = r4.u32;
}

// [[midasm_hook]]s right after the three status-flags queries return: one line
// per change of a voice's flags byte, with its XMA context.
void Amped3_VoiceStateRead() {
  if (!REXCVAR_GET(voice_probe) || !g_state_ptr) return;
  const uint8_t flags = uint8_t(Load32(g_state_ptr) >> 24);
  g_state_ptr = 0;  // One of the sites is also a branch target that skips the call.
  static std::map<std::pair<uint32_t, uint32_t>, int> last;
  std::lock_guard lock(g_mutex);
  const uint32_t context = VoiceContext(g_state_voice);
  if (context < 0xFFCA6000 || context >= 0xFFCA8000) return;  // XMA voices only
  auto [it, inserted] = last.try_emplace({g_state_voice, context}, -1);
  if (it->second == flags) return;
  it->second = flags;
  REXLOG_INFO("VoiceState: voice={:08X} ctx={:08X} flags={:02X}", g_state_voice, context, flags);
}
void Amped3_Voice_82747CE8(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747CE8", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82747D48(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747D48", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82747DA0(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747DA0", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82747DF0(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747DF0", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82747E48(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747E48", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82747EB0(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747EB0", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82747F08(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747F08", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82747F60(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747F60", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82747FB8(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82747FB8", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82748010(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) {
  Record("P82748010", r3.u32, r4.u32, r5.u32, f1.f64);
  if (REXCVAR_GET(voice_probe)) Amped3_VoiceVolume(r3.u32, f1.f64);
}
void Amped3_Voice_82748088(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82748088", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82748100(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82748100", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_827481D8(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P827481D8", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82748258(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82748258", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_82748260(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P82748260", r3.u32, r4.u32, r5.u32, f1.f64); }
void Amped3_Voice_827482C0(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1) { Record("P827482C0", r3.u32, r4.u32, r5.u32, f1.f64); }

// The calling guest thread's return addresses up the back chain.
std::string GuestStack() {
  auto* t = rex::system::XThread::GetCurrentThread();
  if (!t || !t->thread_state() || !t->thread_state()->context()) return "?";
  const auto* ctx = t->thread_state()->context();
  std::string out = fmt::format("{:08X}", uint32_t(ctx->lr));
  uint32_t sp = ctx->r1.u32;
  for (int i = 0; i < 14 && sp >= 0x70000000 && sp < 0x80000000; ++i) {
    const uint32_t caller_sp = Load32(sp);
    if (caller_sp <= sp || caller_sp >= 0x80000000) break;
    out += fmt::format(">{:08X}", Load32(caller_sp - 8));
    sp = caller_sp;
  }
  return out;
}

// [[midasm_hook]] at sub_82424CA0, a sound instance's per-frame volume (r3 =
// instance, f1 = fade progress; volume = lerp(+56, +52, f1)): for XMA voices,
// log when the from/to volumes change, with the stack.
void Amped3_FadeStep(PPCRegister& r3, PPCRegister& f1) {
  if (!REXCVAR_GET(voice_probe)) return;
  const uint32_t voice = Load32(r3.u32 + 264);
  // Other sound-instance types share this method; only use the context offset
  // learned from real SetVolume calls, and only for plausible voice objects.
  if (voice < 0x40000000 || voice >= 0x50000000 || g_context_offset < 0) return;
  std::lock_guard lock(g_mutex);
  const uint32_t context = Load32(voice + uint32_t(g_context_offset));
  if (context < 0xFFCA6000 || context >= 0xFFCA8000) return;
  struct State { uint32_t from = ~0u, to = ~0u; double progress = -1; bool moving = false; };
  static std::map<uint32_t, State> states;
  auto& st = states[r3.u32];
  const uint32_t from_bits = Load32(r3.u32 + 56), to_bits = Load32(r3.u32 + 52);
  if (from_bits != st.from || to_bits != st.to) {
    float from, to;
    std::memcpy(&from, &from_bits, 4);
    std::memcpy(&to, &to_bits, 4);
    REXLOG_INFO("FadeStart: inst={:08X} voice={:08X} ctx={:08X} progress={:.3f} from={:.3f} to={:.3f} stack={}",
                r3.u32, voice, context, f1.f64, from, to, GuestStack());
    st.from = from_bits;
    st.to = to_bits;
  }
  // The category volume factor ([[inst+24]+92]+32, as sub_82424CA0 reads it):
  // log the category object's first 64 bytes whenever its volume changes by
  // more than 0.02, to find its target/current/rate fields.
  if (const uint32_t holder = Load32(r3.u32 + 24)) {
    if (const uint32_t category = Load32(holder + 92)) {
      static std::map<uint32_t, float> category_last;
      const uint32_t bits = Load32(category + 32);
      float value;
      std::memcpy(&value, &bits, 4);
      auto [cit, cinserted] = category_last.try_emplace(category, -1.0f);
      if (std::abs(cit->second - value) > 0.02f || (value == 0.0f && cit->second != 0.0f)) {
        cit->second = value;
        std::string words;
        for (uint32_t off = 0; off < 64; off += 4) words += fmt::format(" {:08X}", Load32(category + off));
        REXLOG_INFO("Category: inst={:08X} ctx={:08X} category={:08X} volume={:.3f} raw:{}", r3.u32,
                    context, category, value, words);
      }
    }
  }
  // Fades run through the progress argument (0 -> 1 in, 1 -> 0 out): log the
  // first step of each, with the caller that is driving it.
  const bool moving = st.progress >= 0 && std::abs(f1.f64 - st.progress) > 1e-6;
  if (moving && !st.moving) {
    REXLOG_INFO("FadeMove: inst={:08X} ctx={:08X} progress {:.3f} -> {:.3f} stack={}", r3.u32,
                context, st.progress, f1.f64, GuestStack());
  }
  st.moving = moving;
  st.progress = f1.f64;
}

// [[midasm_hook]]s on the stores to a sound instance's fade multiplier (+68,
// used when +157 is set): for XMA music-sized voices, log the site, the value
// stored and the stack the first time per (site, instance) and on big changes.
void FadeMultiplierStore(uint32_t site, uint32_t inst, double value) {
  if (!REXCVAR_GET(voice_probe) || g_context_offset < 0) return;
  const uint32_t voice = Load32(inst + 264);
  if (voice < 0x40000000 || voice >= 0x50000000) return;
  std::lock_guard lock(g_mutex);
  const uint32_t context = Load32(voice + uint32_t(g_context_offset));
  if (context < 0xFFCA6000 || context >= 0xFFCA8000) return;
  static std::map<std::pair<uint32_t, uint32_t>, double> last;
  auto [it, inserted] = last.try_emplace({site, inst}, -10.0);
  const bool edge = (value == 0.0 || value == 1.0) && it->second != value;
  if (!inserted && std::abs(it->second - value) < 0.1 && !edge) return;
  it->second = value;
  REXLOG_INFO("FadeMul: site={:08X} inst={:08X} ctx={:08X} value={:.3f} stack={}", site, inst, context,
              value, GuestStack());
}
void Amped3_FadeMul_82423310(PPCRegister& r3, PPCRegister& f0) { FadeMultiplierStore(0x82423310, r3.u32 + 0, f0.f64); }
void Amped3_FadeMul_8242383C(PPCRegister& r30, PPCRegister& f0) { FadeMultiplierStore(0x8242383C, r30.u32 + 0, f0.f64); }
void Amped3_FadeMul_82423854(PPCRegister& r30, PPCRegister& f31) { FadeMultiplierStore(0x82423854, r30.u32 + 0, f31.f64); }
void Amped3_FadeMul_8242675C(PPCRegister& r31, PPCRegister& f0) { FadeMultiplierStore(0x8242675C, r31.u32 + 0, f0.f64); }
void Amped3_FadeMul_8242A6E0(PPCRegister& r31, PPCRegister& f3) { FadeMultiplierStore(0x8242A6E0, r31.u32 + 0, f3.f64); }
void Amped3_FadeMul_8242A6FC(PPCRegister& r31, PPCRegister& f31) { FadeMultiplierStore(0x8242A6FC, r31.u32 + 0, f31.f64); }
void Amped3_FadeMul_82422E90(PPCRegister& r3, PPCRegister& f0) { FadeMultiplierStore(0x82422E90, r3.u32 + 0, f0.f64); }
void Amped3_FadeMul_82429904(PPCRegister& r30, PPCRegister& f0) { FadeMultiplierStore(0x82429904, r30.u32 + 0, f0.f64); }
void Amped3_FadeMul_8242FC20(PPCRegister& r11, PPCRegister& f0) { FadeMultiplierStore(0x8242FC20, r11.u32 + 0, f0.f64); }

// A guest string: tries the pointer as a C string, then as a pointer to one.
std::string GuestString(uint32_t address) {
  for (int indirect = 0; indirect < 3 && address; ++indirect) {
    std::string out;
    bool ok = true;
    for (uint32_t i = 0; i < 64; ++i) {
      const uint32_t word = Load32((address + i) & ~3u);
      if (!word && ((address + i) & 3) == 0 && i == 0) { ok = false; break; }
      const uint8_t c = uint8_t(word >> (8 * (3 - ((address + i) & 3))));
      if (c == 0) break;
      if (c < 32 || c > 126) { ok = false; break; }
      out += char(c);
    }
    if (ok && out.size() >= 3) return out;
    address = Load32(address);
  }
  return "?";
}

// [[midasm_hook]] at sub_824176A8, the sound engine's "run an operation on every
// instance matching a name" (r4 = name, r5 = operation: 3 duck, 4 un-duck, the
// rest dispatch through a table; f1/f2 = values).
void Amped3_SoundOp(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& f1,
                    PPCRegister& f2) {
  if (!REXCVAR_GET(voice_probe)) return;
  REXLOG_INFO("SoundOp: op={} name={} f1={:.3f} f2={:.3f} r3={:08X} stack={}", r5.u32,
              GuestString(r4.u32), f1.f64, f2.f64, r3.u32, GuestStack());
}

std::string GuestBytes(uint32_t address, uint32_t size) {
  std::string out;
  for (uint32_t off = 0; off < size; off += 4) out += fmt::format("{}{:08X}", off ? " " : "", Load32(address + off));
  return out;
}

// [[midasm_hook]] in the stop queue (sub_82415C68), right after the instance-id
// lookup: r3 = found, r31 = request (+4 name, +8 ptr, +12 id, +16 flag).
void Amped3_StopRequest(PPCRegister& r3, PPCRegister& r31) {
  if (!REXCVAR_GET(voice_probe)) return;
  REXLOG_INFO("StopRequest: id={:08X} found={} name={} raw={}", Load32(r31.u32 + 12), r3.u32 & 0xFF,
              GuestString(r31.u32 + 4), GuestBytes(r31.u32, 20));
}

// [[midasm_hook]] at sub_824188B0, the play-request handler (r4 = request).
void Amped3_PlayRequest(PPCRegister& r4) {
  if (!REXCVAR_GET(voice_probe)) return;
  REXLOG_INFO("PlayRequest: name={} raw={} stack={}", GuestString(r4.u32 + 4), GuestBytes(r4.u32, 24),
              GuestStack());
}

// [[midasm_hook]]s on sound-instance vtable (0x82073B0C) slots 5-10 (5 = Stop,
// sub_82424188; the others look like pause/resume/play): instance, its XMA
// context, r4 and the stack, so we see which systems start and stop cues.
void InstanceCall(int slot, uint32_t inst, uint32_t r4) {
  if (!REXCVAR_GET(voice_probe)) return;
  const uint32_t voice = Load32(inst + 264);
  uint32_t context = 0;
  if (voice >= 0x40000000 && voice < 0x50000000 && g_context_offset >= 0) {
    context = Load32(voice + uint32_t(g_context_offset));
  }
  REXLOG_INFO("InstanceCall: slot={} inst={:08X} ctx={:08X} r4={:08X} stack={}", slot, inst, context, r4,
              GuestStack());
}
void Amped3_Instance_5(PPCRegister& r3, PPCRegister& r4) { InstanceCall(5, r3.u32, r4.u32); }
void Amped3_Instance_6(PPCRegister& r3, PPCRegister& r4) { InstanceCall(6, r3.u32, r4.u32); }
void Amped3_Instance_7(PPCRegister& r3, PPCRegister& r4) { InstanceCall(7, r3.u32, r4.u32); }
void Amped3_Instance_8(PPCRegister& r3, PPCRegister& r4) { InstanceCall(8, r3.u32, r4.u32); }
void Amped3_Instance_9(PPCRegister& r3, PPCRegister& r4) { InstanceCall(9, r3.u32, r4.u32); }
void Amped3_Instance_10(PPCRegister& r3, PPCRegister& r4) { InstanceCall(10, r3.u32, r4.u32); }

// [[midasm_hook]] at sub_82414F58, enqueue a stop request (r4 = request:
// +4 name, +8 ptr, +12 instance id, +16 flag).
void Amped3_StopEnqueue(PPCRegister& r4) {
  if (!REXCVAR_GET(voice_probe)) return;
  REXLOG_INFO("StopEnqueue: id={:08X} name={} raw={} stack={}", Load32(r4.u32 + 12), GuestString(r4.u32 + 4),
              GuestBytes(r4.u32, 20), GuestStack());
}

// [[midasm_hook]] at sub_82412070, a sound instance's parameter ramp (r3 = ramp
// at instance+108: +0 owner, +12 start, +20 current, +24 step, +36 action when
// done (1 = stop), +40 active). The ramp does not advance while the owner's
// +154 flag is set. Logs XMA instances whenever any of these change.
void Amped3_Ramp(PPCRegister& r3) {
  if (!REXCVAR_GET(voice_probe) || g_context_offset < 0) return;
  const uint32_t ramp = r3.u32, inst = ramp - 108;
  const uint32_t voice = Load32(inst + 264);
  uint32_t context = 0;
  if (voice >= 0x40000000 && voice < 0x50000000) context = Load32(voice + uint32_t(g_context_offset));
  const uint32_t owner = Load32(ramp);
  const uint32_t flag154 = owner ? (Load32((owner + 154) & ~3u) >> (8 * (3 - ((owner + 154) & 3)))) & 0xFF : 0;
  const std::string state = fmt::format("owner={:08X} p154={} start={:08X} cur={:08X} step={:08X} action={} active={}",
                                        owner, flag154, Load32(ramp + 12), Load32(ramp + 20), Load32(ramp + 24),
                                        Load32(ramp + 36), Load32(ramp + 40));
  std::lock_guard lock(g_mutex);
  static std::map<uint32_t, std::string> last;
  auto& prev = last[inst];
  if (prev == state) return;
  prev = state;
  REXLOG_INFO("Ramp: inst={:08X} ctx={:08X} {}", inst, context, state);
}

// [[midasm_hook]] at sub_82423348, the sound-instance constructor (r3 = new
// instance): its address and the stack, to see who starts each sound.
void Amped3_InstanceNew(PPCRegister& r3) {
  if (!REXCVAR_GET(voice_probe)) return;
  REXLOG_INFO("InstanceNew: inst={:08X} stack={}", r3.u32, GuestStack());
}

// [[midasm_hook]]s on the functions that index the 232-byte play-queue entries:
// each new (function, call stack) pair once, with r4/r5, to find the enqueue.
void PlayQueueFn(uint32_t fn, uint32_t r4, uint32_t r5) {
  if (!REXCVAR_GET(voice_probe)) return;
  const std::string stack = GuestStack();
  std::lock_guard lock(g_mutex);
  static std::set<std::string> seen;
  if (!seen.insert(fmt::format("{:08X}{}", fn, stack)).second) return;
  REXLOG_INFO("PlayQueueFn: fn={:08X} r4={:08X} r5={:08X} stack={}", fn, r4, r5, stack);
}
void Amped3_PlayQ_824140C8(PPCRegister& r4, PPCRegister& r5) { PlayQueueFn(0x824140C8, r4.u32, r5.u32); }
void Amped3_PlayQ_82414418(PPCRegister& r4, PPCRegister& r5) { PlayQueueFn(0x82414418, r4.u32, r5.u32); }
void Amped3_PlayQ_824144C0(PPCRegister& r4, PPCRegister& r5) { PlayQueueFn(0x824144C0, r4.u32, r5.u32); }
void Amped3_PlayQ_82414530(PPCRegister& r4, PPCRegister& r5) { PlayQueueFn(0x82414530, r4.u32, r5.u32); }
void Amped3_PlayQ_824152B8(PPCRegister& r4, PPCRegister& r5) { PlayQueueFn(0x824152B8, r4.u32, r5.u32); }
void Amped3_PlayQ_82416248(PPCRegister& r4, PPCRegister& r5) { PlayQueueFn(0x82416248, r4.u32, r5.u32); }
void Amped3_PlayQ_82416AA0(PPCRegister& r4, PPCRegister& r5) { PlayQueueFn(0x82416AA0, r4.u32, r5.u32); }

// [[midasm_hook]] at sub_82493198, the music state machine: the game state
// ([[0x8288F0A8]+76]: mode +8 (or +4 when +8 is 25), sub-state +12) whenever it
// changes, with the "Menu Soundtrack" handle at 0x8286FA9C.
void Amped3_MusicState() {
  if (!REXCVAR_GET(voice_probe)) return;
  const uint32_t state = Load32(Load32(0x8288F0A8) + 76);
  if (!state) return;
  uint32_t mode = Load32(state + 8);
  if (mode == 25) mode = Load32(state + 4);
  const uint32_t sub = Load32(state + 12);
  static uint64_t last = ~0ull;
  const uint64_t key = (uint64_t(mode) << 32) | sub;
  if (key == last) return;
  last = key;
  REXLOG_INFO("MusicState: mode={} sub={} menu_handle={:08X} other_handle={:08X}", mode, sub,
              Load32(0x8286FA9C), Load32(0x8286FABC));
}

// [[midasm_hook]] at sub_82417DC0 (fade/stop by handle; r4 = handle id).
void Amped3_FadeStopByHandle(PPCRegister& r4) {
  if (!REXCVAR_GET(voice_probe)) return;
  REXLOG_INFO("FadeStopByHandle: id={:08X} menu_handle={:08X} stack={}", r4.u32, Load32(0x8286FA9C),
              GuestStack());
}

// [[midasm_hook]] at sub_82494A18 (plays "Loading Music").
void Amped3_LoadingMusic() {
  if (!REXCVAR_GET(voice_probe)) return;
  REXLOG_INFO("LoadingMusic: menu_handle={:08X} stack={}", Load32(0x8286FA9C), GuestStack());
}
