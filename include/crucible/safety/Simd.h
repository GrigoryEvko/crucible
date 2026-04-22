#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::simd — portable SIMD facade for Crucible
//
// Project policy (memory feedback_simd_strategy.md):
//
// When introducing SIMD into Crucible, pick exactly ONE of two paths:
//
//   Path A — std::simd (preferred for greenfield work)
//     Use GCC 16's <experimental/simd>.  ABI-portable: same source
//     compiles to AVX-512 / AVX2 / SSE / NEON via target dispatch.
//     Lane order + reduction order pinned by spec → preserves DetSafe
//     (bit-identical across ISAs).  Width-pinned aliases below
//     (i64x8, u64x8, etc.) are the typical entry point.
//
//   Path B — Explicit 128/256/512 paths with [[gnu::target_clones]]
//     Required when std::simd doesn't expose the exact intrinsic
//     (e.g., vpshufb, vpternlog, vpcompressd).  Width-by-bits aliases
//     below (simd_128<T>, simd_256<T>, simd_512<T>) and the
//     CRUCIBLE_TARGET_CLONES macro support this.  Existing SwissCtrl-
//     style hand-rolled multi-ABI intrinsics already use this pattern
//     at the intrinsic level.
//
// NEVER ship single-arch intrinsics without a fallback — a binary
// that SIGILLs on a cousin's Skylake breaks the multi-vendor L1 story
// (CRUCIBLE.md L1 hardware).
//
// ─── DetSafe contract (CLAUDE.md §II.8) ─────────────────────────────
//
// Every helper here MUST produce bit-identical output on every
// supported ISA when used inside a BITEXACT recipe.  Concretely:
//
//   * Lane order is stable: SimdT(arr, element_aligned)[i] returns
//     arr[i] regardless of the SIMD width used internally.
//   * Integer ops (add/sub/mul/and/or/xor/shift) are bit-exact across
//     ISAs by IEEE / two's-complement guarantee.
//   * Reductions over integer types use std::plus / std::bit_xor /
//     std::bit_or — associative, identical output regardless of fold
//     order.  max_reduce / min_reduce spill to stack and scalar-fold
//     in lane-index order; deterministic by construction.
//   * Floating-point reductions are NOT provided.  FP rounding is
//     order-sensitive; reductions over float vectors belong in
//     DetSafe-exempt code paths only (canonical reduction tree per
//     CLAUDE.md §II.8 BITEXACT discipline).
//
// ─── Use sites (current and planned) ────────────────────────────────
//
//   SIMD-1   (BackgroundThread/MerkleDag dim-hash) — i64x8 +
//            iota/prefix_mask + xor_reduce
//   SIMD-2   (compute_storage_nbytes overflow chain) — i64x8 +
//            select + add_reduce
//   SIMD-6   (Saturate batch helpers) — u64x8 / i64x8 batch ops
//   SIMD-7   (reflect_fmix_fold_batch) — u64x8 cross-instance fold
//   SIMD-9   (Philox::generate_batch8) — u32x8 (Path B candidate)
//   SIMD-12  (BackgroundThread SIMD batch processor) — combines drain
//            + dim-hash via these primitives
//   SIMD-13  (CMake target_clones build infra) — uses
//            CRUCIBLE_TARGET_CLONES below
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>

#include <experimental/simd>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace crucible::simd {

namespace stdx = std::experimental;

// ── Width-pinned aliases (Path A, lane-count is part of the type) ──
//
// Use these for portable SIMD code.  fixed_size<N> means lane count
// is part of the type; the compiler picks the best implementation
// (one zmm on AVX-512, two ymm on AVX2, four xmm on SSE/NEON).
// Lane[i] semantics are identical across all of them.

using i64x4  = stdx::simd<int64_t,  stdx::simd_abi::fixed_size<4>>;
using i64x8  = stdx::simd<int64_t,  stdx::simd_abi::fixed_size<8>>;
using u64x4  = stdx::simd<uint64_t, stdx::simd_abi::fixed_size<4>>;
using u64x8  = stdx::simd<uint64_t, stdx::simd_abi::fixed_size<8>>;
using i32x8  = stdx::simd<int32_t,  stdx::simd_abi::fixed_size<8>>;
using i32x16 = stdx::simd<int32_t,  stdx::simd_abi::fixed_size<16>>;
using u32x8  = stdx::simd<uint32_t, stdx::simd_abi::fixed_size<8>>;
using u32x16 = stdx::simd<uint32_t, stdx::simd_abi::fixed_size<16>>;
using u8x16  = stdx::simd<uint8_t,  stdx::simd_abi::fixed_size<16>>;
using u8x32  = stdx::simd<uint8_t,  stdx::simd_abi::fixed_size<32>>;

// Mask types for masked reductions / select.
using i64x8_mask = i64x8::mask_type;
using u64x8_mask = u64x8::mask_type;
using u32x8_mask = u32x8::mask_type;

// ── Width-by-bits aliases (Path B explicit-width helpers) ──────────
//
// fixed_width_simd<T, BitWidth> picks the lane count automatically
// from the requested vector bit-width:
//   simd_128<int64_t>  → simd<int64_t, fixed_size<2>>
//   simd_256<int64_t>  → simd<int64_t, fixed_size<4>>
//   simd_512<int64_t>  → simd<int64_t, fixed_size<8>>
//
// Use these in [[gnu::target_clones]] dispatch paths where the code
// wants to think in terms of "the AVX-512 zmm version".  Same lane
// semantics as Path A aliases; just a different spelling.

template <typename T, size_t BitWidth>
  requires (BitWidth % (sizeof(T) * 8) == 0 && BitWidth >= sizeof(T) * 8)
using fixed_width_simd = stdx::simd<T,
    stdx::simd_abi::fixed_size<BitWidth / (sizeof(T) * 8)>>;

template <typename T> using simd_128 = fixed_width_simd<T, 128>;
template <typename T> using simd_256 = fixed_width_simd<T, 256>;
template <typename T> using simd_512 = fixed_width_simd<T, 512>;

// ── Tag re-exports ────────────────────────────────────────────────
//
// Saves call sites from digging into the experimental namespace.

inline constexpr stdx::vector_aligned_tag  vector_aligned{};
inline constexpr stdx::element_aligned_tag element_aligned{};

// ── iota_v<SimdT>() ───────────────────────────────────────────────
//
// SIMD value with lane[i] == i.  The underlying array is local and
// constexpr-folded at every reasonable optimization level; with
// `-O2 -flto` this materializes into a single .rodata constant.
//
// Foundation for masked reductions (see prefix_mask) and per-lane
// table-lookup constants (e.g., kDimMix indexed by lane id).

template <typename SimdT>
[[nodiscard, gnu::const]] constexpr SimdT iota_v() noexcept {
  using T = typename SimdT::value_type;
  std::array<T, SimdT::size()> indices{};
  for (size_t lane = 0; lane < SimdT::size(); ++lane) {
    indices[lane] = static_cast<T>(lane);
  }
  return SimdT{indices.data(), element_aligned};
}

// ── prefix_mask<SimdT>(count) ─────────────────────────────────────
//
// Returns a mask with lanes [0, count) set, lanes [count, size())
// unset.  THE primitive for "process the first N lanes" patterns
// where N is a runtime value.
//
// Use case: TensorMeta::sizes[ndim] dim-hash (ndim ≤ 8), where the
// fixed_size<8> vector covers the worst case but only the first ndim
// lanes carry meaningful data.  prefix_mask(ndim) zeros out the rest
// before reduction.
//
// Compiles to: one materialized iota constant + one vector compare.

template <typename SimdT>
[[nodiscard, gnu::const]] CRUCIBLE_INLINE
typename SimdT::mask_type prefix_mask(size_t count) noexcept {
  using T = typename SimdT::value_type;
  return iota_v<SimdT>() < SimdT(static_cast<T>(count));
}

// ── load_partial<SimdT>(ptr, count) ───────────────────────────────
//
// Load `count` elements from `ptr` into a SimdT, zero-fill the rest.
// Safe when count ≤ SimdT::size() AND ptr+count is in-bounds.
//
// Prefer plain `SimdT{ptr, vector_aligned}` when the full vector
// load is in-bounds and aligned — load_partial is the conservative
// fallback for the bounded-array case where lanes past `count` may
// alias garbage memory.

template <typename SimdT>
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
SimdT load_partial(const typename SimdT::value_type* ptr,
                   size_t count) noexcept {
  using T = typename SimdT::value_type;
  alignas(SimdT) std::array<T, SimdT::size()> buf{};
  for (size_t lane = 0; lane < count; ++lane) {
    buf[lane] = ptr[lane];
  }
  return SimdT{buf.data(), vector_aligned};
}

// ── Masked select ─────────────────────────────────────────────────
//
// Returns lane-by-lane (mask[i] ? on_true[i] : on_false[i]).  Wraps
// the where-expression idiom into a value-returning form for clean
// composition inside expressions.
//
// Both arguments are passed by value; the caller's locals are never
// mutated.  Internally we mutate the local copy of `on_false` so the
// generated code is one blend instruction (vpblendmq on AVX-512).

template <typename SimdT>
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
SimdT select(typename SimdT::mask_type mask,
             SimdT on_true, SimdT on_false) noexcept {
  stdx::where(mask, on_false) = on_true;
  return on_false;
}

// ── Pinned-shape integer reductions (DetSafe-safe) ────────────────
//
// xor_reduce, add_reduce, or_reduce, and_reduce wrap stdx::reduce
// with the void specializations of the std functors (std::plus<>,
// std::bit_xor<>, etc.).  The void specializations are transparent —
// they accept any operand types and dispatch through operator+ /
// operator^ / etc.  This binds cleanly to BOTH scalar T (final tail
// fold) AND simd_chunk<T> (intermediate vector folds) in libstdc++'s
// chunked-fold reduction implementation.  The typed specializations
// std::plus<T> would only bind to scalar T and fail at the chunk
// stage with a "no known conversion from simd<T,...> to const T&"
// error — DO NOT use them here.
//
// max_reduce / min_reduce spill the SIMD vector to a stack-aligned
// array and scalar-fold lane[0]..lane[N-1] in order.  std::experimental
// does not expose portable hmax/hmin, and a generic max/min functor
// doesn't bind to the simd_chunk path because `simd_chunk > simd_chunk`
// returns simd_mask (not bool).  The stack-fold approach is
// deterministic by construction (lane-index order is canonical) and
// compiles to a single horizontal-reduce on AVX-512 + a few vmaxs
// instructions on AVX2; cost ≤ 5 ns for N=8 on Zen4.

template <typename SimdT>
  requires std::integral<typename SimdT::value_type>
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
typename SimdT::value_type
xor_reduce(SimdT v) noexcept {
  return stdx::reduce(v, std::bit_xor<>{});
}

template <typename SimdT>
  requires std::integral<typename SimdT::value_type>
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
typename SimdT::value_type
add_reduce(SimdT v) noexcept {
  return stdx::reduce(v, std::plus<>{});
}

template <typename SimdT>
  requires std::integral<typename SimdT::value_type>
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
typename SimdT::value_type
or_reduce(SimdT v) noexcept {
  return stdx::reduce(v, std::bit_or<>{});
}

template <typename SimdT>
  requires std::integral<typename SimdT::value_type>
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
typename SimdT::value_type
and_reduce(SimdT v) noexcept {
  return stdx::reduce(v, std::bit_and<>{});
}

template <typename SimdT>
  requires std::integral<typename SimdT::value_type>
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
typename SimdT::value_type
max_reduce(SimdT v) noexcept {
  using T = typename SimdT::value_type;
  alignas(SimdT) std::array<T, SimdT::size()> buf{};
  v.copy_to(buf.data(), vector_aligned);
  T result = buf[0];
  for (size_t lane = 1; lane < SimdT::size(); ++lane) {
    if (buf[lane] > result) result = buf[lane];
  }
  return result;
}

template <typename SimdT>
  requires std::integral<typename SimdT::value_type>
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
typename SimdT::value_type
min_reduce(SimdT v) noexcept {
  using T = typename SimdT::value_type;
  alignas(SimdT) std::array<T, SimdT::size()> buf{};
  v.copy_to(buf.data(), vector_aligned);
  T result = buf[0];
  for (size_t lane = 1; lane < SimdT::size(); ++lane) {
    if (buf[lane] < result) result = buf[lane];
  }
  return result;
}

// ── Microarch detection (compile-time) ────────────────────────────
//
// kAvx512Available et al. reflect the BUILD target.  Use in
// `if constexpr` blocks to gate compile-time SIMD path selection
// (e.g., declare a wider type alias when AVX-512 is targeted).

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

// ── Microarch detection (runtime) ─────────────────────────────────
//
// Use to report which SIMD path the [[gnu::target_clones]] resolver
// is actually using (bench harness, diagnostic logging on
// heterogeneous fleets).  Implemented via __builtin_cpu_supports
// which uses GCC's CPUID-based dispatch table; first call may incur
// a one-time CPUID probe.  On non-x86 platforms returns false at
// constexpr time.

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

// ── CRUCIBLE_TARGET_CLONES ────────────────────────────────────────
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
// its own dispatcher local to the call site (e.g., armv8.2-a vs
// armv8.4-a fork).

#if defined(__x86_64__) && !defined(CRUCIBLE_SIMD_NO_TARGET_CLONES)
  #ifndef CRUCIBLE_SIMD_TARGETS
    #define CRUCIBLE_SIMD_TARGETS "default,sse4.2,avx2,avx512f"
  #endif
  #define CRUCIBLE_TARGET_CLONES \
    [[gnu::target_clones(CRUCIBLE_SIMD_TARGETS)]]
#else
  #define CRUCIBLE_TARGET_CLONES
#endif
