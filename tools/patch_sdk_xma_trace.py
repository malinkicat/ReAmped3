"""Debug-only ReXGlue v0.10.0 patch: --xma_trace=true logs the game's XMA context
calls (create/initialize/enable/disable/release/loop data/input buffers).

usage: patch_sdk_xma_trace.py <rexglue-src>
"""
import sys
from pathlib import Path

path = Path(sys.argv[1]) / 'src/kernel/xboxkrnl/xboxkrnl_audio_xma.cpp'
src = path.read_text()
if 'xma_trace' in src:
    print('xboxkrnl_audio_xma.cpp: already patched')
    sys.exit(0)

def rep(old, new):
    global src
    assert src.count(old) == 1, old
    src = src.replace(old, new)

rep('namespace rex::kernel::xboxkrnl {',
    'REXCVAR_DEFINE_BOOL(xma_trace, false, "Audio", "Debug: log XMA context calls");\n'
    '#define XMA_TRACE(...) \\\n  do { if (REXCVAR_GET(xma_trace)) REXKRNL_INFO(__VA_ARGS__); } while (0)\n\n'
    'namespace rex::kernel::xboxkrnl {')
rep('''  *context_out_ptr = context_ptr;
  if (!context_ptr) {''', '''  *context_out_ptr = context_ptr;
  XMA_TRACE("XMA create {:08X}", context_ptr);
  if (!context_ptr) {''')
rep('''  xma_decoder->ReleaseContext(context_ptr.guest_address());''',
    '''  XMA_TRACE("XMA release {:08X} stack={}", context_ptr.guest_address(), GuestStack());
  xma_decoder->ReleaseContext(context_ptr.guest_address());''')
rep('''  std::memset(context_ptr, 0, sizeof(XMA_CONTEXT_DATA));
''', '''  XMA_TRACE("XMA init {:08X} in0={:08X}x{} in1={:08X}x{} out={:08X}x{} ch={} rate={} loop={:X}-{:X} n={}",
            context_ptr.guest_address(), uint32_t(context_init->input_buffer_0_ptr),
            uint32_t(context_init->input_buffer_0_packet_count),
            uint32_t(context_init->input_buffer_1_ptr),
            uint32_t(context_init->input_buffer_1_packet_count), output_buffer_guest_ptr,
            uint32_t(context_init->output_buffer_block_count), uint32_t(context_init->channel_count),
            uint32_t(context_init->sample_rate), uint32_t(context_init->loop_data.loop_start),
            uint32_t(context_init->loop_data.loop_end), context_init->loop_data.loop_count);
  std::memset(context_ptr, 0, sizeof(XMA_CONTEXT_DATA));
''')
rep('''u32 XMASetLoopData_entry(mapped_void context_ptr, ppc_ptr_t<XMA_CONTEXT_DATA> loop_data) {
  XMA_CONTEXT_DATA context(context_ptr);
''', '''u32 XMASetLoopData_entry(mapped_void context_ptr, ppc_ptr_t<XMA_CONTEXT_DATA> loop_data) {
  XMA_CONTEXT_DATA context(context_ptr);
  XMA_TRACE("XMA loop {:08X} {:X}-{:X} n={}", context_ptr.guest_address(),
            uint32_t(loop_data->loop_start), uint32_t(loop_data->loop_end),
            uint32_t(loop_data->loop_count));
''')
rep('''u32 XMAEnableContext_entry(mapped_void context_ptr) {
''', '''u32 XMAEnableContext_entry(mapped_void context_ptr) {
''')
rep('''u32 XMADisableContext_entry(mapped_void context_ptr, u32 wait) {
''', '''u32 XMADisableContext_entry(mapped_void context_ptr, u32 wait) {
''')
for n in ('0', '1'):
    rep('''u32 XMASetInputBuffer%sValid_entry(mapped_void context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
''' % n, '''u32 XMASetInputBuffer%sValid_entry(mapped_void context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  XMA_TRACE("XMA feed%s {:08X} buf={:08X}x{} rd={:X} hash={:08X} stack={}", context_ptr.guest_address(),
            uint32_t(context.input_buffer_%s_ptr), uint32_t(context.input_buffer_%s_packet_count),
            uint32_t(context.input_buffer_read_offset),
            BufferHash(context.input_buffer_%s_ptr, context.input_buffer_%s_packet_count), GuestStack());
''' % (n, n, n, n, n, n))
rep('namespace rex::kernel::xboxkrnl {', '''namespace rex::kernel::xboxkrnl {

// --xma_trace: FNV-1a of an input buffer's packets (2048 bytes each, physical
// address), to tell whether a stream replays the same data (loops).
uint32_t BufferHash(uint32_t physical, uint32_t packets) {
  const auto* p = REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(physical);
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < packets * 2048; ++i) {
    h = (h ^ p[i]) * 16777619u;
  }
  return h;
}

// --xma_trace: the calling guest thread's return addresses (lr, then the saved
// lr of each frame up the back chain), to find the game code behind a call.
std::string GuestStack() {
  auto* thread = rex::system::XThread::GetCurrentThread();
  if (!thread || !thread->thread_state() || !thread->thread_state()->context()) {
    return "?";
  }
  const auto* ctx = thread->thread_state()->context();
  auto* memory = REX_KERNEL_MEMORY();
  auto load = [memory](uint32_t address) {
    const auto* p = memory->TranslateVirtual<const uint8_t*>(address);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  };
  std::string out = fmt::format("{:08X}", uint32_t(ctx->lr));
  uint32_t sp = ctx->r1.u32;
  for (int i = 0; i < 12 && sp >= 0x70000000 && sp < 0x80000000; ++i) {
    const uint32_t caller_sp = load(sp);
    if (caller_sp <= sp || caller_sp >= 0x80000000) {
      break;
    }
    out += fmt::format(">{:08X}", load(caller_sp - 8));
    sp = caller_sp;
  }
  return out;
}

// --xma_trace: most XMA calls come every audio frame, too often to log one by
// one; count them per context and function and log the counts once a second.
void XmaTraceCount(uint32_t context, const char* function) {
  static std::mutex mutex;
  static std::map<std::pair<uint32_t, std::string>, uint32_t> counts;
  static auto window_start = std::chrono::steady_clock::now();
  std::lock_guard lock(mutex);
  ++counts[{context, function}];
  const auto now = std::chrono::steady_clock::now();
  if (now - window_start < std::chrono::seconds(1)) {
    return;
  }
  std::string out;
  for (const auto& [key, n] : counts) {
    out += fmt::format(" {:08X}:{}={}", key.first, key.second, n);
  }
  REXKRNL_INFO("XMA calls per second:{}", out);
  counts.clear();
  window_start = now;
}''')
import re as _re
def _count(m):
    name = m.group(1)
    return m.group(0) + f'  if (REXCVAR_GET(xma_trace)) XmaTraceCount(context_ptr.guest_address(), "{name[3:]}");\n'
src, _n = _re.subn(r'u32 (XMA\w+)_entry\(mapped_void context_ptr[^)]*\) \{\n', _count, src)
assert _n >= 18, _n
rep('#include <rex/system/xtypes.h>\n', '#include <rex/system/thread_state.h>\n#include <rex/system/xthread.h>\n#include <rex/system/xtypes.h>\n')
for inc in ['#include <chrono>', '#include <map>', '#include <mutex>', '#include <string>']:
    if inc not in src:
        src = src.replace('#include <cstring>\n', '#include <cstring>\n' + inc + '\n', 1)
if '#include <rex/cvar.h>' not in src:
    rep('#include <rex/logging.h>', '#include <rex/cvar.h>\n#include <rex/logging.h>')
path.write_text(src)
print('xboxkrnl_audio_xma.cpp: patched')
