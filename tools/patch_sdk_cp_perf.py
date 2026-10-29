"""ReXGlue v0.10.0 patch: cheaper per-register and per-draw work in the GPU command
processor, which limits frame rate above 60 FPS (85% busy at ~100 FPS).

- CommandProcessor::WriteRegister looked every register up in RegisterFile's
  big switch only to log unknown ones; use a bitmap built once instead.
- RequestRanges took the global lock (also taken constantly by game threads)
  on every draw just to find no pages to upload; check without it first.
- Type-0 packets wrote consecutive registers one virtual call at a time; use
  the range path, which copies whole constant ranges at once (as Xenia Canary).
- SharedMemory::RequestRanges allocated a vector on every call (every draw);
  reuse a per-thread one.

usage: patch_sdk_cp_perf.py <rexglue-src>
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])

def patch(rel, pairs, marker='amped3 cp perf'):
    path = root / rel
    src = path.read_text()
    if marker in src:
        print(f'{rel}: already patched')
        return
    for old, new in pairs:
        assert src.count(old) == 1, (rel, old)
        src = src.replace(old, new)
    if rel.endswith('shared_memory.cpp') and '#include <atomic>' not in src:
        src = src.replace('#include <algorithm>\n', '#include <algorithm>\n#include <atomic>\n', 1)
    path.write_text(src)
    print(f'{rel}: patched')

patch('src/graphics/command_processor.cpp', [(
'''  if (!regs.GetRegisterInfo(index)) {
    REXGPU_DEBUG("GPU: Write to unknown register ({:04X} = {:08X})", index, value);
  }''',
'''  // amped3 cp perf: known registers from a bitmap built once, not the switch.
  static const auto known_registers = [] {
    std::vector<bool> known(RegisterFile::kRegisterCount);
    for (uint32_t i = 0; i < RegisterFile::kRegisterCount; ++i) {
      known[i] = RegisterFile::GetRegisterInfo(i) != nullptr;
    }
    return known;
  }();
  if (!known_registers[index]) {
    REXGPU_DEBUG("GPU: Write to unknown register ({:04X} = {:08X})", index, value);
  }''')])

patch('src/graphics/command_processor.cpp', [('  uint32_t base_index = (packet & 0x7FFF);\n  uint32_t write_one_reg = (packet >> 15) & 0x1;\n  for (uint32_t m = 0; m < count; m++) {\n    uint32_t reg_data = reader->ReadAndSwap<uint32_t>();\n    uint32_t target_index = write_one_reg ? base_index : base_index + m;\n    WriteRegister(target_index, reg_data);\n  }', '  uint32_t base_index = (packet & 0x7FFF);\n  uint32_t write_one_reg = (packet >> 15) & 0x1;\n  if (!write_one_reg) {\n    // amped3 cp perf: consecutive registers through the range path, which\n    // copies whole constant ranges at once (as in Xenia Canary).\n    WriteRegisterRangeFromRing(reader, base_index, count);\n    return true;\n  }\n  for (uint32_t m = 0; m < count; m++) {\n    WriteRegister(base_index, reader->ReadAndSwap<uint32_t>());\n  }')], marker='consecutive registers through the range path')

patch('src/graphics/shared_memory.cpp', [(
'''  // Some texture or buffer is empty, for example - safe to draw in this case.
  std::vector<std::pair<uint32_t, uint32_t>> merged_ranges;
  merged_ranges.reserve(count);''',
'''  // Some texture or buffer is empty, for example - safe to draw in this case.
  // amped3 cp perf: reuse a per-thread vector instead of allocating per draw.
  static thread_local std::vector<std::pair<uint32_t, uint32_t>> spare_ranges;
  std::vector<std::pair<uint32_t, uint32_t>> merged_ranges = std::move(spare_ranges);
  merged_ranges.clear();
  struct ReturnSpare {
    std::vector<std::pair<uint32_t, uint32_t>>& ranges;
    ~ReturnSpare() { spare_ranges = std::move(ranges); }
  } return_spare{merged_ranges};
  merged_ranges.reserve(count);'''), ('  {\n    auto global_lock = global_critical_region_.Acquire();\n    for (const std::pair<uint32_t, uint32_t>& range : merged_ranges) {\n      uint32_t page_first = range.first >> page_size_log2_;', '  // amped3 cp perf: when every requested page is already valid (the common\n  // case), nothing needs uploading and the global lock - which game threads also\n  // take constantly - is skipped. A game thread invalidates pages before writing\n  // them and only then submits the draw through the ring buffer, which this\n  // thread reads with acquire ordering, so a relevant invalidation is visible.\n  bool all_valid = true;\n  for (const std::pair<uint32_t, uint32_t>& range : merged_ranges) {\n    uint32_t page_first = range.first >> page_size_log2_;\n    uint32_t page_last = (range.first + range.second - 1) >> page_size_log2_;\n    for (uint32_t i = page_first >> 6; all_valid && i <= (page_last >> 6); ++i) {\n      uint64_t needed = UINT64_MAX;\n      if (i == (page_first >> 6)) {\n        needed &= ~((uint64_t(1) << (page_first & 63)) - 1);\n      }\n      if (i == (page_last >> 6) && (page_last & 63) != 63) {\n        needed &= (uint64_t(1) << ((page_last & 63) + 1)) - 1;\n      }\n      const uint64_t valid =\n          std::atomic_ref<uint64_t>(system_page_flags_valid_[i]).load(std::memory_order_acquire);\n      all_valid = (valid & needed) == needed;\n    }\n    if (!all_valid) {\n      break;\n    }\n  }\n  if (!all_valid) {\n    auto global_lock = global_critical_region_.Acquire();\n    for (const std::pair<uint32_t, uint32_t>& range : merged_ranges) {\n      uint32_t page_first = range.first >> page_size_log2_;')])
