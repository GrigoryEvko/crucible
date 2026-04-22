#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::simd — minimal SIMD facade over C++26 <simd>
//
// Project policy (memory feedback_simd_strategy.md): TWO orthogonal axes.
//
//   Axis 1 — algorithmic path
//     Default:  std::simd from <simd> (P1928R15 + P3441R2).  GCC 16's
//               libstdc++ ships working std::simd::vec / partial_load /
//               unchecked_load / reduce / reduce_max / reduce_min /
//               select / chunk; the __cpp_lib_simd FTM is not yet
//               bumped, but the implementation is gated on internal
//               __glibcxx_simd which fires under our toolchain.  Don't
//               gate on __cpp_lib_simd.
//
//     Fallback: hand-rolled intrinsics with #ifdef __AVX2__ etc., for
//               algorithms std::simd doesn't express (vpshufb,
//               vpternlog, vpcompressd, vpgatherdd, vpopcntq).  See
//               SwissCtrl for the canonical pattern.
//
//   Axis 2 — dispatch path
//     Single-target (-march=native, dev) or multi-target via
//     [[gnu::target_clones]] (production fleet).  Crucible ships
//     across AMD Zen4/5, Intel SPR/EMR/Skylake, ARM Graviton 2+,
//     Apple Silicon — production binaries are always multi-target.
//     CRUCIBLE_TARGET_CLONES below provides the macro.
//
// ─── Why the facade is so thin ──────────────────────────────────────
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
// We add only what std lacks AND what the project repeatedly needs:
//
//   * Width-pinned type aliases (i64x8, u64x8, etc.) — tied to the
//     8-lane TensorMeta dim-hash convention; std forces you to spell
//     vec<int64_t, 8> at every use site.
//   * Width-by-bits aliases (simd_128/256/512<T>) — Path B explicit
//     vector width helper.
//   * iota_v<V>() — std::simd has no iota primitive.  Foundation for
//     prefix_mask and per-lane table indexing.
//   * prefix_mask<V>(count) — std has no "first N lanes" mask helper.
//     Critical for the TensorMeta::sizes[ndim] dim-hash pattern.
//   * Microarch detection (compile-time constants + runtime probes)
//     for benchmarks and target_clones diagnostics.
//   * CRUCIBLE_TARGET_CLONES macro for the production multi-target
//     dispatch policy.
//
// ─── DetSafe contract (CLAUDE.md §II.8) ─────────────────────────────
//
// SIMD code in BITEXACT recipes MUST produce bit-identical output on
// every supported ISA.  Two project-wide rules:
//
//   1. Integer reductions only.  std::simd::reduce on int / uint
//      types is associative + commutative + bit-exact across ISAs.
//      FP reductions are forbidden — IEEE rounding is order-sensitive
//      and std::simd's chunked-fold reordering would diverge across
//      AVX-512 vs AVX2 vs NEON.  The DetSafeSimd concept below
//      expresses this constraint at a single point of definition;
//      apply it at boundary helpers that wrap reduce.
//
//   2. Stable lane order.  std::simd::vec<T, N>(span, flag)[i]
//      returns ptr[i] regardless of the SIMD width selected by the
//      ABI.  Per-lane operations preserve this property; chunk<>()
//      and any cross-lane shuffle does not — never rely on lane
//      identity after a shuffle in a BITEXACT recipe.
//
// ─── Use sites (current and planned) ────────────────────────────────
//
//   SIMD-1   (BackgroundThread/MerkleDag dim-hash) — i64x8 +
//            iota/prefix_mask + std::simd::reduce(vec, mask, xor, 0)
//   SIMD-2   (compute_storage_nbytes overflow chain) — i64x8 +
//            std::simd::select + std::simd::reduce
//   SIMD-6   (Saturate batch helpers) — u64x8 / i64x8 batch ops
//   SIMD-7   (reflect_fmix_fold_batch) — u64x8 cross-instance fold
//   SIMD-9   (Philox::generate_batch8) — u32x8 (Path B candidate)
//   SIMD-12  (BackgroundThread SIMD batch processor)
//   SIMD-13  (CMake target_clones build infra)
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <simd>
#include <type_traits>

namespace crucible::simd {

// ── Width-pinned aliases (terse names for the common cases) ────────
//
// These reflect the lane counts Crucible actually uses (TensorMeta
// has ndim ≤ 8, so i64x8 is the natural size for dim-hash).  std
// forces you to spell `std::simd::vec<int64_t, 8>` at every use.

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

// Mask aliases for the matching widths.
using i64x8_mask = i64x8::mask_type;
using u64x8_mask = u64x8::mask_type;
using u32x8_mask = u32x8::mask_type;

// ── Width-by-bits aliases (Path B explicit-width helpers) ──────────
//
// fixed_width_simd<T, BitWidth> picks lane count from vector width:
//   simd_128<int64_t> → vec<int64_t, 2>   (one xmm)
//   simd_256<int64_t> → vec<int64_t, 4>   (one ymm)
//   simd_512<int64_t> → vec<int64_t, 8>   (one zmm)
//
// Use these in [[gnu::target_clones]] dispatch paths where the code
// wants to think in terms of "the AVX-512 zmm version".

template <typename T, size_t BitWidth>
  requires (BitWidth % (sizeof(T) * 8) == 0 && BitWidth >= sizeof(T) * 8)
using fixed_width_simd = std::simd::vec<T, BitWidth / (sizeof(T) * 8)>;

template <typename T> using simd_128 = fixed_width_simd<T, 128>;
template <typename T> using simd_256 = fixed_width_simd<T, 256>;
template <typename T> using simd_512 = fixed_width_simd<T, 512>;

// ── DetSafeSimd<V> concept ─────────────────────────────────────────
//
// Single-source-of-truth predicate: V is a SIMD vector whose lane
// type can take part in a BITEXACT-recipe reduction.  Currently
// "integral lane" — FP reductions would diverge across ISAs because
// std::simd's chunked-fold reorders operations.
//
// Apply at boundary helpers that wrap std::simd::reduce when the
// caller needs a compile-time guarantee.  Inside hot loops, callers
// invoke std::simd::reduce directly — code review enforces the
// integer rule there.

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
//
// constexpr-folds to a single .rodata constant under -O2 -flto.
// Foundation for prefix_mask and per-lane table-index constants.

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
// is a runtime value.
//
// Use case: TensorMeta::sizes[ndim] dim-hash where ndim ≤ 8 and the
// fixed_size<8> vector covers the worst case but only the first ndim
// lanes carry meaningful data.  Pair with the masked-reduce overload
// for one-call masked aggregation:
//
//   auto valid = simd::prefix_mask<u64x8>(meta.ndim);
//   auto h = std::simd::reduce(contributions, valid, std::bit_xor<>{}, 0ULL);
//
// Compiles to: one materialized iota constant + one vector compare.

template <typename V>
[[nodiscard, gnu::const]] CRUCIBLE_INLINE
typename V::mask_type prefix_mask(int count) noexcept {
  using T = typename V::value_type;
  return iota_v<V>() < V(static_cast<T>(count));
}

// ── Microarch detection (compile-time) ─────────────────────────────
//
// Reflect the BUILD target.  Use in `if constexpr` blocks to gate
// compile-time SIMD path selection (e.g., declare a wider type alias
// when AVX-512 is targeted).

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
// Use to report which SIMD path the [[gnu::target_clones]] resolver
// is actually using (bench harness, diagnostic logging on
// heterogeneous fleets).  __builtin_cpu_supports uses GCC's CPUID-
// based dispatch table; first call may incur a one-time CPUID probe.

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

// ── CRUCIBLE_TARGET_CLONES ─────────────────────────────────────────
//
// Path B dispatch macro.  Apply to a free function (NOT a member
// function — target_clones doesn't compose with class scope) to emit
// multiple specializations: one per ISA tier the build manifest
// declares.  The function body compiles N times; the resolver picks
// the best at first call.  Subsequent calls are direct.
//
// Default tier list covers the x86 deployment matrix.  Override per
// build via -DCRUCIBLE_SIMD_TARGETS="..." if a fleet wants different
// tiers (e.g., stricter AVX-512 minimum).
//
// On non-x86 (ARM, etc.) the macro expands to nothing.  Single-target
// compilation is correct because GCC 16's ARM target_clones support
// is less mature than x86's; ARM-specific Path B work should declare
// its own dispatcher local to the call site.

#if defined(__x86_64__) && !defined(CRUCIBLE_SIMD_NO_TARGET_CLONES)
  #ifndef CRUCIBLE_SIMD_TARGETS
    #define CRUCIBLE_SIMD_TARGETS "default,sse4.2,avx2,avx512f"
  #endif
  #define CRUCIBLE_TARGET_CLONES \
    [[gnu::target_clones(CRUCIBLE_SIMD_TARGETS)]]
#else
  #define CRUCIBLE_TARGET_CLONES
#endif
