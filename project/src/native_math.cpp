// native_math.cpp - Native versions of hot game math on the main thread
//
// sub_821790A0 multiplies two row-major 4x4 float matrices: out (r3) = a (r4)
// * b (r5). It is ~12% of the game's main thread at the busy open-world spot on
// the Steam Deck. The recompiled version copies both matrices to the stack,
// transposes b with vector merges and takes 16 vmsum4fp128 dot products; the
// recompiler implements vmsum4fp128 as _mm_dp_ps(a, b, 0xFF), so doing the same
// dot products here gives bit-identical results.
//
// --native_matrix_multiply_verify: the game's own version runs too, and the
// results are compared bit for bit (logged every 100000 calls).

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <immintrin.h>

#include <atomic>
#include <cstring>

REXCVAR_DEFINE_BOOL(native_matrix_multiply, true, "CPU",
                    "Run the game's 4x4 matrix multiply (sub_821790A0) natively");
REXCVAR_DEFINE_BOOL(native_matrix_multiply_verify, false, "CPU",
                    "Debug: also run the game's matrix multiply and compare the results");

REX_EXTERN(__imp__sub_821790A0);

namespace {

// As the recompiled code's REX_RAW_ADDR (the 0xE0000000+ heap is offset on
// Windows).
inline uint8_t* Host(uint8_t* base, uint32_t address) {
  return base + address + (address >= 0xE0000000u ? 0x1000u : 0u);
}

const __m128i kSwap32 =
    _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);

inline __m128 LoadRow(const uint8_t* p) {
  return _mm_castsi128_ps(
      _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)), kSwap32));
}

inline void StoreRow(uint8_t* p, __m128 v) {
  _mm_storeu_si128(reinterpret_cast<__m128i*>(p), _mm_shuffle_epi8(_mm_castps_si128(v), kSwap32));
}

// out = a * b, rows of 16 bytes, big-endian. Inputs are read before any
// output is written (out may be a or b). dp_ps rounds the four products, then
// adds them as (p0 + p1) + (p2 + p3); the same here with a's elements
// broadcast against b's rows, so all four columns at once (dpps is slow
// microcode on Zen 2: this was ~9% of the main thread on the Steam Deck).
void Multiply(uint8_t* base, uint32_t out, uint32_t a, uint32_t b, uint8_t* out_host) {
  __m128 a_rows[4], b_rows[4];
  for (uint32_t i = 0; i < 4; ++i) {
    a_rows[i] = LoadRow(Host(base, a + 16 * i));
    b_rows[i] = LoadRow(Host(base, b + 16 * i));
  }
  for (uint32_t i = 0; i < 4; ++i) {
    const __m128 r = a_rows[i];
    const __m128 p0 = _mm_mul_ps(_mm_shuffle_ps(r, r, _MM_SHUFFLE(0, 0, 0, 0)), b_rows[0]);
    const __m128 p1 = _mm_mul_ps(_mm_shuffle_ps(r, r, _MM_SHUFFLE(1, 1, 1, 1)), b_rows[1]);
    const __m128 p2 = _mm_mul_ps(_mm_shuffle_ps(r, r, _MM_SHUFFLE(2, 2, 2, 2)), b_rows[2]);
    const __m128 p3 = _mm_mul_ps(_mm_shuffle_ps(r, r, _MM_SHUFFLE(3, 3, 3, 3)), b_rows[3]);
    const __m128 row = _mm_add_ps(_mm_add_ps(p0, p1), _mm_add_ps(p2, p3));
    StoreRow(out_host ? out_host + 16 * i : Host(base, out + 16 * i), row);
  }
}

std::atomic<uint64_t> g_verified{0}, g_mismatches{0};

}  // namespace

extern "C" REX_FUNC(sub_821790A0) {
  if (!REXCVAR_GET(native_matrix_multiply)) {
    __imp__sub_821790A0(ctx, base);
    return;
  }
  // The same flush-to-zero state the recompiled vector code runs with.
  ctx.fpscr.enableFlushMode();
  const uint32_t out = ctx.r3.u32, a = ctx.r4.u32, b = ctx.r5.u32;
  if (!REXCVAR_GET(native_matrix_multiply_verify)) {
    Multiply(base, out, a, b, nullptr);
    return;
  }
  alignas(16) uint8_t native[64];
  Multiply(base, out, a, b, native);
  __imp__sub_821790A0(ctx, base);
  uint64_t n = g_verified.fetch_add(1, std::memory_order_relaxed) + 1;
  if (std::memcmp(native, Host(base, out), 64)) {
    g_mismatches.fetch_add(1, std::memory_order_relaxed);
  }
  if (!(n % 100000)) {
    REXLOG_INFO("native_matrix_multiply_verify: {} calls, {} mismatches", n,
                g_mismatches.load(std::memory_order_relaxed));
  }
}

// sub_821F99D8 transforms a point by a 4x4 matrix with the perspective divide:
// out (r3, 3 floats) = (x R0 + y R1 + z R2 + R3).xyz / w, in (r4) = x y z, the
// matrix's rows at r5. The recompiled version copies the matrix to the stack
// and runs vmaddfp (a multiply, then an add: two roundings) and vrefp (a
// division in the recompiler) refined by two Newton steps with a NaN guard;
// the same operations here give bit-identical results.
// --native_transform_verify compares with the game's own (logged every 100000).
REXCVAR_DEFINE_BOOL(native_transform, true, "CPU",
                    "Run the game's point-by-matrix transform with perspective divide (sub_821F99D8) natively");
REXCVAR_DEFINE_BOOL(native_transform_verify, false, "CPU",
                    "Debug: also run the game's transform and compare the results");

REX_EXTERN(__imp__sub_821F99D8);

namespace {

inline float LoadFloat(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

inline void StoreFloat(uint8_t* p, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = __builtin_bswap32(v);
  std::memcpy(p, &v, 4);
}

inline void StoreBE32(uint8_t* p, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(p, &v, 4);
}

// The transformed point's x, y, z, big-endian, into out12.
void Transform(uint8_t* base, uint32_t in, uint32_t m, uint8_t* out12) {
  const uint8_t* p = Host(base, in);
  const __m128 x = _mm_set1_ps(LoadFloat(p)), y = _mm_set1_ps(LoadFloat(p + 4)),
               z = _mm_set1_ps(LoadFloat(p + 8));
  const __m128 r0 = LoadRow(Host(base, m)), r1 = LoadRow(Host(base, m + 16)),
               r2 = LoadRow(Host(base, m + 32)), r3 = LoadRow(Host(base, m + 48));
  __m128 v = _mm_add_ps(_mm_mul_ps(r2, z), r3);
  v = _mm_add_ps(_mm_mul_ps(y, r1), v);
  v = _mm_add_ps(_mm_mul_ps(x, r0), v);
  const __m128 w = _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 3, 3, 3));
  const __m128 one = _mm_set1_ps(1.0f), sign = _mm_castsi128_ps(_mm_set1_epi32(int(0x80000000)));
  const __m128 e = _mm_div_ps(one, w);                                   // vrefp
  __m128 t = _mm_xor_ps(_mm_sub_ps(_mm_mul_ps(w, e), one), sign);        // vnmsubfp
  t = _mm_add_ps(_mm_mul_ps(e, t), e);                                    // vmaddfp
  const __m128 u = _mm_xor_ps(_mm_sub_ps(_mm_mul_ps(w, t), one), sign);  // vnmsubfp
  const __m128 ordered = _mm_cmpeq_ps(t, t);                             // vcmpeqfp
  t = _mm_add_ps(_mm_mul_ps(t, u), t);                                    // vmaddfp
  const __m128 r = _mm_or_ps(_mm_andnot_ps(ordered, e), _mm_and_ps(ordered, t));  // vsel
  alignas(16) float o[4];
  _mm_store_ps(o, _mm_mul_ps(v, r));
  StoreFloat(out12, o[0]);
  StoreFloat(out12 + 4, o[1]);
  StoreFloat(out12 + 8, o[2]);
}

std::atomic<uint64_t> g_transform_verified{0}, g_transform_mismatches{0};

}  // namespace

extern "C" REX_FUNC(sub_821F99D8) {
  if (!REXCVAR_GET(native_transform)) {
    __imp__sub_821F99D8(ctx, base);
    return;
  }
  ctx.fpscr.enableFlushMode();
  const uint32_t out = ctx.r3.u32, in = ctx.r4.u32, m = ctx.r5.u32;
  // Its stores to the parameter save area, as the game's version makes them.
  StoreBE32(Host(base, ctx.r1.u32 + 20), out);
  StoreBE32(Host(base, ctx.r1.u32 + 28), in);
  StoreBE32(Host(base, ctx.r1.u32 + 36), m);
  uint8_t result[12];
  Transform(base, in, m, result);
  if (!REXCVAR_GET(native_transform_verify)) {
    std::memcpy(Host(base, out), result, 12);
    return;
  }
  __imp__sub_821F99D8(ctx, base);
  const uint64_t n = g_transform_verified.fetch_add(1, std::memory_order_relaxed) + 1;
  if (std::memcmp(result, Host(base, out), 12)) {
    const uint64_t bad = g_transform_mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (bad <= 5) {
      const uint8_t* g = Host(base, out);
      REXLOG_INFO("native_transform_verify: mismatch: native {:08X} {:08X} {:08X}, game {:08X} {:08X} {:08X}",
                  __builtin_bswap32(*reinterpret_cast<const uint32_t*>(result)),
                  __builtin_bswap32(*reinterpret_cast<const uint32_t*>(result + 4)),
                  __builtin_bswap32(*reinterpret_cast<const uint32_t*>(result + 8)),
                  __builtin_bswap32(*reinterpret_cast<const uint32_t*>(g)),
                  __builtin_bswap32(*reinterpret_cast<const uint32_t*>(g + 4)),
                  __builtin_bswap32(*reinterpret_cast<const uint32_t*>(g + 8)));
    }
  }
  if (!(n % 100000)) {
    REXLOG_INFO("native_transform_verify: {} calls, {} mismatches", n,
                g_transform_mismatches.load(std::memory_order_relaxed));
  }
}

// sub_8244ED58 tests a sphere against four planes: r4 = the sphere (centre at
// +4, +8, +12, radius at +16), r5 = the planes (rows at +16, +32, +48, +64, as
// x y z d); r3 = 1 if it's on the inner side of all four, else 0. The game
// computes each plane's distance with vmsum4fp128 (a dp_ps in the recompiler,
// under flush to zero), adds the radius in single precision (flush off), and
// counts the planes where that's negative (or NaN) with fsel and double adds
// of the constants 1.0 and 0.0 at 0x82089478; the same steps here.
REXCVAR_DEFINE_BOOL(native_sphere_test, true, "CPU",
                    "Run the game's sphere-against-planes visibility test (sub_8244ED58) natively");
REXCVAR_DEFINE_BOOL(native_sphere_test_verify, false, "CPU",
                    "Debug: also run the game's sphere test and compare the results");

REX_EXTERN(__imp__sub_8244ED58);

namespace {

uint32_t SphereVisible(PPCContext& ctx, uint8_t* base, uint32_t sphere, uint32_t planes) {
  const uint8_t* p = Host(base, sphere);
  const float radius = LoadFloat(p + 16);
  const __m128 point = _mm_setr_ps(LoadFloat(p + 4), LoadFloat(p + 8), LoadFloat(p + 12), 1.0f);
  // The four planes' dot products at once, rounded as dp_ps does: products,
  // then (x + y) + (z + w) (dpps is slow microcode on Zen 2).
  __m128 rows[4];
  for (int i = 0; i < 4; ++i) rows[i] = LoadRow(Host(base, (planes + 16 * (i + 1)) & ~0xFu));
  _MM_TRANSPOSE4_PS(rows[0], rows[1], rows[2], rows[3]);
  alignas(16) float distance[4];
  ctx.fpscr.enableFlushModeUnconditional();
  const __m128 xy = _mm_add_ps(_mm_mul_ps(rows[0], _mm_shuffle_ps(point, point, _MM_SHUFFLE(0, 0, 0, 0))),
                               _mm_mul_ps(rows[1], _mm_shuffle_ps(point, point, _MM_SHUFFLE(1, 1, 1, 1))));
  const __m128 zw = _mm_add_ps(_mm_mul_ps(rows[2], _mm_shuffle_ps(point, point, _MM_SHUFFLE(2, 2, 2, 2))),
                               _mm_mul_ps(rows[3], _mm_shuffle_ps(point, point, _MM_SHUFFLE(3, 3, 3, 3))));
  _mm_store_ps(distance, _mm_add_ps(xy, zw));
  ctx.fpscr.disableFlushModeUnconditional();
  int outside = 0;
  for (float d : distance) {
    const float sum = radius + d;
    if (!(sum >= 0.0f)) ++outside;
  }
  return outside > 0 ? 0 : 1;
}

std::atomic<uint64_t> g_sphere_verified{0}, g_sphere_mismatches{0};

}  // namespace

extern "C" REX_FUNC(sub_8244ED58) {
  if (!REXCVAR_GET(native_sphere_test)) {
    __imp__sub_8244ED58(ctx, base);
    return;
  }
  const uint32_t sphere = ctx.r4.u32, planes = ctx.r5.u32;
  const uint32_t visible = SphereVisible(ctx, base, sphere, planes);
  if (!REXCVAR_GET(native_sphere_test_verify)) {
    ctx.r3.u64 = visible;
    return;
  }
  __imp__sub_8244ED58(ctx, base);
  const uint64_t n = g_sphere_verified.fetch_add(1, std::memory_order_relaxed) + 1;
  if (ctx.r3.u32 != visible) {
    if (g_sphere_mismatches.fetch_add(1, std::memory_order_relaxed) < 5) {
      REXLOG_INFO("native_sphere_test_verify: mismatch: native {} game {} (sphere {:08X} planes {:08X})", visible,
                  ctx.r3.u32, sphere, planes);
    }
  }
  if (!(n % 1000000)) {
    REXLOG_INFO("native_sphere_test_verify: {} calls, {} mismatches", n,
                g_sphere_mismatches.load(std::memory_order_relaxed));
  }
}

// sub_827D7900 is the CRT memcpy (r3 = destination, r4 = source, r5 = byte
// count; returns r3), ~3% of the main thread in the open world on the Steam
// Deck. Byte copies don't depend on endianness, so the host's copy does the
// same (memmove, as overlapping copies with the guest's forward loop are left
// to chance either way).
REXCVAR_DEFINE_BOOL(native_memcpy, true, "CPU", "Run the game's memcpy (sub_827D7900) natively");

REX_EXTERN(__imp__sub_827D7900);

extern "C" REX_FUNC(sub_827D7900) {
  const uint32_t dest = ctx.r3.u32, src = ctx.r4.u32, size = ctx.r5.u32;
  // The 0xE0000000+ heap is offset on the host: a copy must not straddle it.
  if (!REXCVAR_GET(native_memcpy) || !size ||
      ((dest < 0xE0000000u) != (dest + size - 1 < 0xE0000000u)) ||
      ((src < 0xE0000000u) != (src + size - 1 < 0xE0000000u))) {
    __imp__sub_827D7900(ctx, base);
    return;
  }
  std::memmove(Host(base, dest), Host(base, src), size);
}
