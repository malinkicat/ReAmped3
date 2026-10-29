// native_inflate.cpp - Native zlib inflate_fast for the game's decompression
//
// The game decompresses its assets with zlib 1.2.3; inflate_fast (the hot loop
// of inflate, sub_822008B8) is most of a loading thread's time, and runs much
// faster native than recompiled. This is zlib 1.2.3's inffast.c (pre-increment
// variant, OFF = 1, not INFLATE_STRICT), reading and writing the game's
// big-endian z_stream / inflate_state in guest memory. Hooked at its entry
// with return_on_true (--native_inflate, on by default).
//
// --native_inflate_verify: the game's own inflate_fast runs instead, and right
// after it returns (0x821FFC98 in inflate, r23 = strm) the native version is
// run from a snapshot of the inputs into a scratch buffer and compared - output
// bytes, stream and state fields. Mismatches are logged.
//
// Altered (ported) from zlib 1.2.3 inffast.c, Copyright (C) 1995-2004 Mark Adler;
// see THIRD_PARTY_NOTICES.md.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include <cstdint>
#include <cstring>
#include <vector>

REXCVAR_DEFINE_BOOL(native_inflate, true, "General",
                    "Run the game's zlib decompression loop (inflate_fast) natively");
REXCVAR_DEFINE_BOOL(native_inflate_verify, false, "Debug",
                    "Run the game's own inflate_fast and compare the native one against it");

namespace {

// z_stream (32-bit): next_in 0, avail_in 4, total_in 8, next_out 12,
// avail_out 16, total_out 20, msg 24, state 28.
// inflate_state: mode 0, wsize 40, whave 44, write 48, window 52, hold 56,
// bits 60, lencode 76, distcode 80, lenbits 84, distbits 88.
constexpr uint32_t kModeType = 11;
constexpr uint32_t kModeBad = 27;
constexpr uint32_t kMsgInvalidLength = 0x8205577C;    // "invalid literal/length code"
constexpr uint32_t kMsgInvalidDistance = 0x82055798;  // "invalid distance code"
constexpr uint32_t kMsgTooFarBack = 0x820557B0;       // "invalid distance too far back"

rex::memory::Memory* g_memory = nullptr;

// Guest addresses don't all map to host memory at the same offset (the
// 0xE0000000+ heap has its own on Windows), so each buffer gets its own base:
// host = base + guest address, valid within one allocation.
uint8_t* RegionBase(uint32_t address) {
  return g_memory->TranslateVirtual<uint8_t*>(address) - address;
}

uint32_t Load32(uint32_t address) {
  const uint8_t* p = g_memory->TranslateVirtual<const uint8_t*>(address);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
void Store32(uint32_t address, uint32_t value) {
  uint8_t* p = g_memory->TranslateVirtual<uint8_t*>(address);
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

struct Code {
  uint8_t op, bits;
  uint16_t val;
};
Code LoadCode(const uint8_t* table_base, uint32_t table, uint32_t index) {
  const uint8_t* p = table_base + table + index * 4;
  return Code{p[0], p[1], uint16_t((uint32_t(p[2]) << 8) | p[3])};
}

// Results the caller writes back to the stream and state.
struct Result {
  uint32_t next_in, avail_in, next_out, avail_out, hold, bits;
  bool set_mode = false;
  uint32_t mode = 0;
  bool set_msg = false;
  uint32_t msg = 0;
};

// Output bytes: written straight to guest memory, or (Scratch) to a buffer
// standing in for [out_start, out_start + scratch size), with reads of the
// output before it from guest memory.
template <bool Scratch>
struct Output {
  uint8_t* base = nullptr;  // RegionBase of the output buffer.
  uint32_t start = 0;
  std::vector<uint8_t>* scratch = nullptr;
  uint8_t Read(uint32_t address) const {
    if constexpr (Scratch) {
      if (address >= start && address - start < scratch->size()) {
        return (*scratch)[address - start];
      }
    }
    return base[address];
  }
  void Write(uint32_t address, uint8_t value) {
    if constexpr (Scratch) {
      (*scratch)[address - start] = value;
    } else {
      base[address] = value;
    }
  }
};

// Addresses are guest addresses (pointer arithmetic as in inffast.c).
template <bool Scratch>
Result InflateFast(uint32_t next_in, uint32_t avail_in, uint32_t next_out, uint32_t avail_out,
                   uint32_t state, uint32_t start, Output<Scratch>& output) {
  uint32_t in = next_in - 1;
  const uint32_t last = in + (avail_in - 5);
  uint32_t out = next_out - 1;
  const uint32_t beg = out - (start - avail_out);
  const uint32_t end = out + (avail_out - 257);
  const uint32_t wsize = Load32(state + 40);
  const uint32_t whave = Load32(state + 44);
  const uint32_t write = Load32(state + 48);
  const uint32_t window = Load32(state + 52);
  uint32_t hold = Load32(state + 56);
  uint32_t bits = Load32(state + 60);
  const uint32_t lcode = Load32(state + 76);
  const uint32_t dcode = Load32(state + 80);
  const uint32_t lmask = (1u << Load32(state + 84)) - 1;
  const uint32_t dmask = (1u << Load32(state + 88)) - 1;
  const uint8_t* in_base = RegionBase(next_in);
  const uint8_t* window_base = RegionBase(window);
  const uint8_t* lcode_base = RegionBase(lcode);
  const uint8_t* dcode_base = RegionBase(dcode);
  output.base = RegionBase(next_out);
  Result result;
  auto pull = [&]() {
    hold += uint32_t(in_base[++in]) << bits;
    bits += 8;
  };
  auto put = [&](uint8_t value) { output.Write(++out, value); };

  do {
    if (bits < 15) {
      pull();
      pull();
    }
    Code here = LoadCode(lcode_base, lcode, hold & lmask);
    uint32_t op;
  dolen:
    op = here.bits;
    hold >>= op;
    bits -= op;
    op = here.op;
    if (op == 0) {
      put(uint8_t(here.val));
    } else if (op & 16) {
      uint32_t len = here.val;
      op &= 15;
      if (op) {
        if (bits < op) {
          pull();
        }
        len += hold & ((1u << op) - 1);
        hold >>= op;
        bits -= op;
      }
      if (bits < 15) {
        pull();
        pull();
      }
      here = LoadCode(dcode_base, dcode, hold & dmask);
    dodist:
      op = here.bits;
      hold >>= op;
      bits -= op;
      op = here.op;
      if (op & 16) {
        uint32_t dist = here.val;
        op &= 15;
        if (bits < op) {
          pull();
          if (bits < op) {
            pull();
          }
        }
        dist += hold & ((1u << op) - 1);
        hold >>= op;
        bits -= op;
        op = out - beg;
        if (dist > op) {
          // Copy from the window.
          op = dist - op;
          if (op > whave) {
            result.set_msg = true;
            result.msg = kMsgTooFarBack;
            result.set_mode = true;
            result.mode = kModeBad;
            break;
          }
          uint32_t from = window - 1;
          // Whether the rest is copied from the output (after a window part).
          bool from_output = false;
          auto copy_window = [&](uint32_t count) {
            do {
              put(window_base[++from]);
            } while (--count);
          };
          if (write == 0) {
            from += wsize - op;
            if (op < len) {
              len -= op;
              copy_window(op);
              from = out - dist;
              from_output = true;
            }
          } else if (write < op) {
            from += wsize + write - op;
            op -= write;
            if (op < len) {
              len -= op;
              copy_window(op);
              from = window - 1;
              if (write < len) {
                op = write;
                len -= op;
                copy_window(op);
                from = out - dist;
                from_output = true;
              }
            }
          } else {
            from += write - op;
            if (op < len) {
              len -= op;
              copy_window(op);
              from = out - dist;
              from_output = true;
            }
          }
          // The rest comes from the window or (after the window part) from the
          // output; the output may overlap the bytes being written.
          auto next_from = [&]() -> uint8_t {
            ++from;
            return from_output ? output.Read(from) : window_base[from];
          };
          while (len > 2) {
            put(next_from());
            put(next_from());
            put(next_from());
            len -= 3;
          }
          if (len) {
            put(next_from());
            if (len > 1) {
              put(next_from());
            }
          }
        } else {
          // Copy from the output.
          uint32_t from = out - dist;
          do {
            put(output.Read(++from));
            put(output.Read(++from));
            put(output.Read(++from));
            len -= 3;
          } while (len > 2);
          if (len) {
            put(output.Read(++from));
            if (len > 1) {
              put(output.Read(++from));
            }
          }
        }
      } else if ((op & 64) == 0) {
        here = LoadCode(dcode_base, dcode, here.val + (hold & ((1u << op) - 1)));
        goto dodist;
      } else {
        result.set_msg = true;
        result.msg = kMsgInvalidDistance;
        result.set_mode = true;
        result.mode = kModeBad;
        break;
      }
    } else if ((op & 64) == 0) {
      here = LoadCode(lcode_base, lcode, here.val + (hold & ((1u << op) - 1)));
      goto dolen;
    } else if (op & 32) {
      result.set_mode = true;
      result.mode = kModeType;
      break;
    } else {
      result.set_msg = true;
      result.msg = kMsgInvalidLength;
      result.set_mode = true;
      result.mode = kModeBad;
      break;
    }
  } while (in < last && out < end);

  // Return unused bytes (on entry, bits < 8, so in won't go too far back).
  const uint32_t unused = bits >> 3;
  in -= unused;
  bits -= unused << 3;
  hold &= (1u << bits) - 1;
  result.next_in = in + 1;
  result.next_out = out + 1;
  result.avail_in = in < last ? 5 + (last - in) : 5 - (in - last);
  result.avail_out = out < end ? 257 + (end - out) : 257 - (out - end);
  result.hold = hold;
  result.bits = bits;
  return result;
}

void Apply(uint32_t strm, uint32_t state, const Result& r) {
  Store32(strm + 0, r.next_in);
  Store32(strm + 4, r.avail_in);
  Store32(strm + 12, r.next_out);
  Store32(strm + 16, r.avail_out);
  Store32(state + 56, r.hold);
  Store32(state + 60, r.bits);
  if (r.set_msg) {
    Store32(strm + 24, r.msg);
  }
  if (r.set_mode) {
    Store32(state + 0, r.mode);
  }
}

// --native_inflate_verify: the inputs of the call in progress.
struct Snapshot {
  bool valid = false;
  uint32_t strm, state, start;
  uint32_t next_in, avail_in, next_out, avail_out, hold, bits, mode, msg;
};
thread_local Snapshot t_snapshot;
uint64_t g_verified = 0, g_mismatches = 0;

}  // namespace

// [[midasm_hook]] at 0x822008B8, inflate_fast(strm = r3, start = r4).
// Returns true when it did the work (the game's version is skipped).
bool Amped3_InflateFast(PPCRegister& r3, PPCRegister& r4) {
  if (!g_memory) {
    g_memory = REX_KERNEL_MEMORY();
  }
  const uint32_t strm = r3.u32;
  const uint32_t state = Load32(strm + 28);
  if (REXCVAR_GET(native_inflate_verify)) {
    Snapshot& s = t_snapshot;
    s = {true,
         strm,
         state,
         r4.u32,
         Load32(strm + 0),
         Load32(strm + 4),
         Load32(strm + 12),
         Load32(strm + 16),
         Load32(state + 56),
         Load32(state + 60),
         Load32(state + 0),
         Load32(strm + 24)};
    return false;
  }
  if (!REXCVAR_GET(native_inflate)) {
    return false;
  }
  Output<false> output;
  const Result result = InflateFast<false>(Load32(strm + 0), Load32(strm + 4), Load32(strm + 12),
                                           Load32(strm + 16), state, r4.u32, output);
  Apply(strm, state, result);
  return true;
}

// [[midasm_hook]] at 0x821FFC98 in inflate, right after inflate_fast returns
// (r23 = strm): --native_inflate_verify compares the native result.
void Amped3_InflateFastDone(PPCRegister& r23) {
  Snapshot& s = t_snapshot;
  if (!s.valid || s.strm != r23.u32) {
    return;
  }
  s.valid = false;
  // The game's output, now in [next_out, new next_out).
  const uint32_t produced = s.avail_out - Load32(s.strm + 16);
  std::vector<uint8_t> scratch(produced + 258);
  Output<true> output;
  output.start = s.next_out;
  output.scratch = &scratch;
  // Native run from the inputs: hold/bits as on entry (state + 56/60 were
  // overwritten by the game's run, so put the entry values back meanwhile).
  const uint32_t game_hold = Load32(s.state + 56), game_bits = Load32(s.state + 60);
  Store32(s.state + 56, s.hold);
  Store32(s.state + 60, s.bits);
  const Result r =
      InflateFast<true>(s.next_in, s.avail_in, s.next_out, s.avail_out, s.state, s.start, output);
  Store32(s.state + 56, game_hold);
  Store32(s.state + 60, game_bits);
  bool same = r.next_in == Load32(s.strm + 0) && r.avail_in == Load32(s.strm + 4) &&
              r.next_out == Load32(s.strm + 12) && r.avail_out == Load32(s.strm + 16) &&
              r.hold == game_hold && r.bits == game_bits &&
              (r.set_mode ? r.mode : s.mode) == Load32(s.state + 0) &&
              (r.set_msg ? r.msg : s.msg) == Load32(s.strm + 24);
  if (same && std::memcmp(scratch.data(), g_memory->TranslateVirtual<const uint8_t*>(s.next_out),
                          produced) != 0) {
    same = false;
  }
  ++g_verified;
  if (!same) {
    ++g_mismatches;
    REXLOG_ERROR("NativeInflate: MISMATCH (call {}): out {:08X}+{} in {:08X}+{}", g_verified,
                 s.next_out, produced, s.next_in, s.avail_in);
  }
  if ((g_verified & 1023) == 0 || !same) {
    REXLOG_INFO("NativeInflate: verified {} calls, {} mismatches", g_verified, g_mismatches);
  }
}
