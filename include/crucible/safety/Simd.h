#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::simd — minimal SIMD facade over C++26 <simd>
//
// Default: std::simd from <simd> (P1928R15 + P3441R2).  GCC 16's
// libstdc++ ships working std::simd::vec / partial_load /
// unchecked_load / reduce / reduce_max / reduce_min / select /
// chunk; the __cpp_lib_simd FTM is not yet bumped, but the
// implementation is gated on internal __glibcxx_simd which fires
// under our toolchain.  Don't gate on __cpp_lib_simd.
//
// Builds are single-target: `-march=native` on dev, the fleet's
// declared minimum microarch in production.  No multi-target binary
// dispatch; CMake selects one ISA per build.
//
// Hand-rolled intrinsics are acceptable ONLY when std::simd cannot
// express the operation at all (e.g., byte-level compare-to-bitmask
// via `vpcmpeqb + vpmovmskb` — see SwissTable.h for the canonical
// example).  New SIMD code defaults to std::simd.
//
// ─── Why the facade is thin ─────────────────────────────────────────
//
// std::simd already provides nearly everything Crucible needs:
//
//   loads:       std::simd::unchecked_load<V>(ptr, V::size(), flag)
//                std::simd::partial_load<V>(ptr, count, flag)
//   stores:      std::simd::unchecked_store(v, ptr, flag)
//   select:      std::simd::select(mask, on_true, on_false)
//   reduce:      std::simd::reduce(v, op)              // unmasked
//                std::simd::reduce(v, mask, op, id)    // MASKED (one call!)
//                std::simd::reduce_max(v) / reduce_min(v)
//   alg:         std::simd::min/max/minmax/clamp
//   alignment:   std::simd::flag_aligned / flag_default / flag_overaligned<N>
//   chunking:    std::simd::chunk<N>(v)
//
// We add only what std lacks AND the project repeatedly needs:
//
//   * Width-pinned type aliases (i64x8, u64x8, etc.) tied to the
//     8-lane TensorMeta dim-hash convention.  std forces you to spell
//     vec<int64_t, 8> at every use site.
//   * iota_v<V>() — std::simd has no iota primitive.  Foundation for
//     prefix_mask and per-lane table indexing.
//   * prefix_mask<V>(count) — std has no "first N lanes" mask helper.
//     Critical for the TensorMeta::sizes[ndim] dim-hash pattern.
//   * Microarch detection — bench-harness + diagnostic reporting.
//
// ─── DetSafe contract (CLAUDE.md §II.8) ─────────────────────────────
//
//   1. Integer reductions only.  std::simd::reduce on int / uint is
//      associative + commutative + bit-exact across ISAs.  FP
//      reductions are forbidden — IEEE rounding is order-sensitive
//      and std::simd's chunked-fold reordering would diverge across
//      AVX-512 vs AVX2.  The DetSafeSimd concept below expresses
//      this constraint; apply at boundary helpers that wrap reduce.
//
//   2. Stable lane order.  std::simd::vec<T, N>(span, flag)[i]
//      returns ptr[i] regardless of the SIMD width selected by the
//      ABI.  Per-lane operations preserve this; chunk<>() and any
//      cross-lane shuffle does not — never rely on lane identity
//      after a shuffle in a BITEXACT recipe.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <simd>
#include <type_traits>

namespace crucible::simd {

// ── Width-pinned aliases ───────────────────────────────────────────
//
// Reflect the lane counts Crucible actually uses (TensorMeta has
// ndim ≤ 8, so i64x8 is the natural size for dim-hash).  std forces
// you to spell `std::simd::vec<int64_t, 8>` at every use.

using i64x4  = std::simd::vec<int64_t,  4>;
using i64x8  = std::simd::vec<int64_t,  8>;
using u64x4  = std::simd::vec<uint64_t, 4>;
using u64x8  = std::simd::vec<uint64_t, 8>;
using i32x8  = std::simd::vec<int32_t,  8>;
using i32x16 = std::simd::vec<int32_t, 16>;
using u32x8  = std::simd::vec<uint32_t, 8>;
using u32x16 = std::simd::vec<uint32_t,16>;
using u8x16  = std::simd::vec<uint8_t, 16>;
using u8x32  = std::simd::vec<uint8_t, 32>;

using i64x8_mask = i64x8::mask_type;
using u64x8_mask = u64x8::mask_type;
using u32x8_mask = u32x8::mask_type;

// ── DetSafeSimd<V> concept ─────────────────────────────────────────
//
// V is a SIMD vector whose lane type can take part in a BITEXACT-
// recipe reduction.  Currently "integral lane" — FP reductions would
// diverge across ISAs because std::simd's chunked-fold reorders
// operations.  Apply at boundary helpers that wrap std::simd::reduce
// when callers need a compile-time guarantee.

template <typename V>
concept DetSafeSimd =
    requires { typename V::value_type; } &&
    std::integral<typename V::value_type>;

// ── iota_v<V>() ────────────────────────────────────────────────────
//
// SIMD value with lane[i] == i.  std::simd has no public iota
// primitive (libstdc++ has __iota<V> but it's a private name).
//
// Built via basic_vec's generator constructor:
//   template<class G> constexpr explicit basic_vec(G&& gen) noexcept;
// which calls gen(integral_constant<int, lane>) for each lane at
// compile time.  basic_vec::operator[] is value-returning const, so
// subscript-assignment is not an option.

template <typename V>
[[nodiscard, gnu::const]] constexpr V iota_v() noexcept {
  using T = typename V::value_type;
  return V([](auto lane) noexcept -> T {
    return static_cast<T>(decltype(lane)::value);
  });
}

// ── prefix_mask<V>(count) ──────────────────────────────────────────
//
// Mask with lanes [0, count) set, lanes [count, V::size()) unset.
// THE primitive for the "process the first N lanes" pattern when N
// is a runtime value.  Pair with the masked-reduce overload for
// one-call masked aggregation:
//
//   auto valid = simd::prefix_mask<u64x8>(meta.ndim);
//   auto h = std::simd::reduce(contributions, valid, std::bit_xor<>{}, 0ULL);

template <typename V>
[[nodiscard, gnu::const]] CRUCIBLE_INLINE
typename V::mask_type prefix_mask(int count) noexcept {
  using T = typename V::value_type;
  return iota_v<V>() < V(static_cast<T>(count));
}

// ── Microarch detection (compile-time) ─────────────────────────────
//
// Reflect the BUILD target.  Use in `if constexpr` blocks to gate
// compile-time SIMD path selection OR in diagnostic printing (bench
// harness headers).

#if defined(__AVX512F__)
inline constexpr bool kAvx512Available = true;
#else
inline constexpr bool kAvx512Available = false;
#endif

#if defined(__AVX2__)
inline constexpr bool kAvx2Available = true;
#else
inline constexpr bool kAvx2Available = false;
#endif

#if defined(__SSE4_2__)
inline constexpr bool kSse42Available = true;
#else
inline constexpr bool kSse42Available = false;
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
inline constexpr bool kNeonAvailable = true;
#else
inline constexpr bool kNeonAvailable = false;
#endif

// ── Microarch detection (runtime) ──────────────────────────────────
//
// Bench harness + diagnostic reporting: what does this CPU actually
// support?  __builtin_cpu_supports uses GCC's CPUID-based dispatch
// table; first call may incur a one-time CPUID probe.

#if defined(__x86_64__) || defined(__i386__)
[[nodiscard, gnu::pure]] inline bool runtime_supports_avx512() noexcept {
  return __builtin_cpu_supports("avx512f");
}
[[nodiscard, gnu::pure]] inline bool runtime_supports_avx2() noexcept {
  return __builtin_cpu_supports("avx2");
}
[[nodiscard, gnu::pure]] inline bool runtime_supports_sse42() noexcept {
  return __builtin_cpu_supports("sse4.2");
}
#else
[[nodiscard]] constexpr bool runtime_supports_avx512() noexcept { return false; }
[[nodiscard]] constexpr bool runtime_supports_avx2()   noexcept { return false; }
[[nodiscard]] constexpr bool runtime_supports_sse42()  noexcept { return false; }
#endif

}  // namespace crucible::simd
