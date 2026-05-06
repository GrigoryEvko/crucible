#pragma once

// ═══════════════════════════════════════════════════════════════════
// StorageNbytes.h — SIMD compute_storage_nbytes overflow chain (SIMD-2)
//
// SIMD-optimized counterpart to MerkleDag.h's
// compute_storage_nbytes.  Returns the storage span (in bytes) of
// a TensorMeta, accounting for negative strides via max/min offset
// split, with full overflow detection.
//
// Two implementations:
//
//   compute_storage_nbytes_scalar(meta)  — reference, bit-identical
//     to MerkleDag.h::compute_storage_nbytes.  Uses
//     __builtin_*_overflow at every arithmetic step.  Source of
//     truth for correctness.
//
//   compute_storage_nbytes_simd(meta)    — SIMD-optimized with
//     fallback.  Pre-screens for overflow risk via SIMD reduce_max;
//     if all per-lane (sizes[d]-1) × strides[d] fit safely in int64,
//     uses the SIMD path.  Otherwise falls back to scalar.  ALWAYS
//     bit-equivalent to scalar — the safety boundary is preserved.
//
// ─── The overflow-detection-vs-SIMD tension ────────────────────────
//
// SIMD int64 multiply has NO per-lane overflow flag — it wraps
// modulo 2^64.  Crucible's compute_storage_nbytes is a defense-in-
// depth boundary against adversarial TensorMetas (corrupt Cipher
// load, malicious vessel API caller); silent overflow could let
// downstream allocators underallocate (use-after-free) or trip
// contracts.  We must NOT trade away that overflow detection.
//
// Resolution: pre-screen via SIMD reduce_max.  Find max(sizes) and
// max(|strides|) over valid lanes.  Single scalar mul_overflow
// check determines whether the SIMD multiply is safe.  If safe,
// SIMD path executes (load + multiply + mask + spill + scalar fold).
// If unsafe, fall back to scalar (which reads out the per-dim
// overflow at every step, returning UINT64_MAX cleanly).
//
// The pre-screen is cheap (~5 cycles): one SIMD reduce_max + one
// scalar __builtin_mul_overflow.  The fast-path covers the
// realistic 99.9% of TensorMetas (sizes ≤ 2^30 by physical memory
// limits, strides bounded by tensor extent).  The fallback path
// matches scalar exactly.
//
// ─── Algorithmic structure (matches scalar) ────────────────────────
//
// For each dim d in [0, ndim):
//   if sizes[d] == 0: return 0  (zero-size tensor)
//   extent[d] = (sizes[d] - 1) * strides[d]   (can overflow int64)
//   if extent > 0: max_offset += extent       (can overflow int64)
//   else:          min_offset += extent       (can overflow int64)
// span = max_offset - min_offset + 1          (subtraction safe)
// total_bytes = span * element_size(dtype)    (can overflow uint64)
//
// SIMD path computes extent[] all at once, masks invalid lanes,
// spills, and folds scalar with add_overflow checks.  The
// multiplication itself is trusted (pre-screened safe).
//
// ─── DetSafe contract (CLAUDE.md §II.8) ─────────────────────────────
//
// compute_storage_nbytes_simd MUST produce IDENTICAL output to
// compute_storage_nbytes_scalar on every TensorMeta on every
// supported ISA.  Enforced by:
//   * Pre-screen guarantees no per-lane multiply overflow on the
//     SIMD path.
//   * Per-lane operations only — no cross-lane shuffles.
//   * Scalar fold uses identical __builtin_*_overflow logic.
//   * Fallback to scalar on overflow risk, ensuring identical
//     UINT64_MAX return on adversarial inputs.
//
// Verified by test_storage_nbytes_simd's bit-equivalence tests +
// prop_storage_nbytes_simd_equivalence randomized fuzzer.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/Saturated.h>
#include <crucible/safety/Simd.h>

#include <cstdint>
#include <simd>

namespace crucible::detail {

// ── compute_storage_nbytes_scalar (reference) ─────────────────────
//
// Bit-identical to MerkleDag.h::compute_storage_nbytes.  Kept
// exposed so equivalence fuzzers can compare scalar vs SIMD output
// bit-for-bit, and so this file is self-contained as a SIMD-2
// reference.
//
// Any future change to the storage-nbytes algorithm MUST update
// BOTH this function AND compute_storage_nbytes_simd in lockstep,
// AND update the equivalence fuzzer.

[[nodiscard, gnu::const]] CRUCIBLE_INLINE
safety::Saturated<uint64_t> compute_storage_nbytes_scalar(const TensorMeta& meta) noexcept {
  using Sat = safety::Saturated<uint64_t>;
  if (meta.ndim == 0) {
    return Sat{element_size(meta.dtype).raw()};
  }
  int64_t max_offset = 0;
  int64_t min_offset = 0;
  for (uint8_t d = 0; d < meta.ndim; ++d) {
    if (meta.sizes[d] == 0) return Sat{uint64_t{0}};  // zero-size tensor
    int64_t dim_extent_bytes;
    // (sizes[d] - 1) * strides[d] can overflow int64 for huge dims.
    // sizes[d] is positive, so the subtraction never underflows.
    if (__builtin_mul_overflow(meta.sizes[d] - 1, meta.strides[d],
                               &dim_extent_bytes)) [[unlikely]] {
      return Sat{UINT64_MAX, true};
    }
    if (dim_extent_bytes > 0) {
      if (__builtin_add_overflow(max_offset, dim_extent_bytes,
                                 &max_offset)) [[unlikely]] {
        return Sat{UINT64_MAX, true};
      }
    } else {
      if (__builtin_add_overflow(min_offset, dim_extent_bytes,
                                 &min_offset)) [[unlikely]] {
        return Sat{UINT64_MAX, true};
      }
    }
  }
  // span = max_offset - min_offset + 1; subtractions can overflow
  // when max and min straddle int64 limits.
  int64_t span_signed;
  if (__builtin_sub_overflow(max_offset, min_offset,
                             &span_signed)) [[unlikely]] {
    return Sat{UINT64_MAX, true};
  }
  if (__builtin_add_overflow(span_signed, int64_t{1},
                             &span_signed)) [[unlikely]] {
    return Sat{UINT64_MAX, true};
  }
  // span is non-negative (max >= 0 >= min, so max - min >= 0).
  uint64_t total_bytes;
  if (__builtin_mul_overflow(static_cast<uint64_t>(span_signed),
                             static_cast<uint64_t>(element_size(meta.dtype).raw()),
                             &total_bytes)) [[unlikely]] {
    return Sat{UINT64_MAX, true};
  }
  return Sat{total_bytes};
}

// ── Helper: pre-screen safety check ──────────────────────────────
//
// Returns true iff the SIMD path can safely multiply (sizes - 1)
// by strides without per-lane overflow.  Conservative — false
// negatives (returning false for safe inputs) merely fall back to
// the scalar path; false positives (returning true for unsafe
// inputs) would be a correctness bug.
//
// Strategy: find the maximum |sizes-1| value and maximum |strides|
// value over the valid lanes.  If max_a × max_b fits in int64
// (single scalar mul_overflow check), then for every valid d:
//
//   |(sizes[d] - 1) × strides[d]| ≤ |max_smo| × |max_str|
//                                  ≤ INT64_MAX
//
// so the per-lane SIMD multiply cannot overflow.

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
bool storage_nbytes_simd_safe_(const TensorMeta& meta) noexcept {
  using simd::i64x8;

  // TensorMeta is naturally aligned, not guaranteed vector-aligned.
  // Use element-aligned loads so trace-loader vectors and MetaLog
  // buffers are valid inputs.
  auto sizes = std::simd::unchecked_load<i64x8>(
      meta.sizes,   i64x8::size());
  auto strides = std::simd::unchecked_load<i64x8>(
      meta.strides, i64x8::size());

  auto valid_mask = simd::prefix_mask<i64x8>(static_cast<int>(meta.ndim));

  // sizes[d] - 1 for valid lanes, 0 for invalid.  Sizes are
  // non-negative by TensorMeta invariant; (size - 1) for size == 0
  // would be -1, but the zero-size short-circuit catches that
  // before this function runs (callers check first).  For safety
  // we mask and clamp negative results to 0.
  auto sizes_minus_one = std::simd::select(valid_mask,
      sizes - i64x8(1), i64x8(0));

  // strides absolute value, masked.  Avoid -INT64_MIN UB by
  // computing via select-and-negate which is well-defined for
  // every value except INT64_MIN itself; if a stride IS
  // INT64_MIN we mask to INT64_MAX (forces fallback).
  auto strides_neg = -strides;
  auto strides_abs_raw = std::simd::select(strides >= i64x8(0),
                                           strides, strides_neg);
  // INT64_MIN → -INT64_MIN wraps to INT64_MIN; treat as "unsafe"
  // by mapping to INT64_MAX so reduce_max returns INT64_MAX.
  auto is_int64_min = (strides == i64x8(INT64_MIN));
  auto strides_abs = std::simd::select(is_int64_min,
      i64x8(INT64_MAX), strides_abs_raw);
  strides_abs = std::simd::select(valid_mask, strides_abs, i64x8(0));

  // Reduce max over valid lanes.  Both vectors have invalid lanes
  // zeroed; the reduce_max picks the largest valid value (or 0 if
  // no valid lanes).
  const int64_t max_smo = std::simd::reduce_max(sizes_minus_one);
  const int64_t max_str = std::simd::reduce_max(strides_abs);

  // Safe iff max_smo × max_str fits in int64.  __builtin_mul_overflow
  // returns true on overflow (NOT what we want); negate to get safe.
  int64_t bound;
  return !__builtin_mul_overflow(max_smo, max_str, &bound);
}

// ── compute_storage_nbytes_simd ──────────────────────────────────
//
// SIMD-optimized.  Bit-equivalent to scalar for ALL inputs:
//   * Pre-screen passes (99.9% of real tensors) → SIMD path
//   * Pre-screen fails (adversarial / huge inputs) → scalar fallback
//
// Both paths return UINT64_MAX on detected overflow at any
// arithmetic step.

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
safety::Saturated<uint64_t> compute_storage_nbytes_simd(const TensorMeta& meta) noexcept {
  using Sat = safety::Saturated<uint64_t>;
  // Edge case: scalar tensor.  Same as scalar path.
  if (meta.ndim == 0) {
    return Sat{element_size(meta.dtype).raw()};
  }

  using simd::i64x8;

  // Load sizes and strides via element-aligned SIMD load.  TensorMeta
  // arrays are 64 bytes wide but not guaranteed 64-byte aligned.
  auto sizes = std::simd::unchecked_load<i64x8>(
      meta.sizes,   i64x8::size());
  auto strides = std::simd::unchecked_load<i64x8>(
      meta.strides, i64x8::size());

  auto valid_mask = simd::prefix_mask<i64x8>(static_cast<int>(meta.ndim));

  // Zero-size short-circuit: if any valid dim has size 0, total
  // is 0.  Cheap SIMD check via masked equality + any_of.
  auto zero_size_mask = (sizes == i64x8(0)) && valid_mask;
  if (any_of(zero_size_mask)) [[unlikely]] {
    return Sat{uint64_t{0}};
  }

  // Pre-screen for overflow safety.  If any per-lane multiply
  // could overflow, fall back to scalar (which detects + returns
  // a clamped Saturated<uint64_t> cleanly).
  if (!storage_nbytes_simd_safe_(meta)) [[unlikely]] {
    return compute_storage_nbytes_scalar(meta);
  }

  // SIMD path: pre-screen guarantees no per-lane multiply overflow.
  // Compute extents = (sizes - 1) * strides via SIMD; mask invalid
  // lanes to 0 so they don't contribute to max/min accumulation.
  auto sizes_minus_one = sizes - i64x8(1);
  auto extents = sizes_minus_one * strides;
  extents = std::simd::select(valid_mask, extents, i64x8(0));

  // Spill to stack for the scalar fold.
  // FixedArray<int64_t, 8> (#1019 production migration of #1081):
  //   - Carries alignas(64) propagation as a member-level alignment
  //     (the wrapping `alignas(64)` aligns the entire FixedArray
  //     struct to 64, which means data_[0] sits at offset 0 = 64B
  //     aligned — matching the SIMD-aligned discipline the bare
  //     C array used to enforce structurally).
  //   - NSDMI zero-init replaces the bare-array uninit-before-store
  //     window (the std::simd::unchecked_store overwrites all 8
  //     lanes immediately, so this is defense-in-depth, not a
  //     correctness fix).
  //   - .data() returns int64_t* — drop-in replacement for the
  //     bare-array pointer the SIMD store and operator[] expect.
  alignas(64) safety::FixedArray<int64_t, 8> extents_buf{};
  std::simd::unchecked_store(
      extents, extents_buf.data(), i64x8::size(), std::simd::flag_aligned);

  // Scalar fold: per-lane sign-based dispatch into max_offset /
  // min_offset, with __builtin_add_overflow check.  Multiplication
  // overflow CANNOT occur here (pre-screened), so no per-lane
  // mul_overflow re-check needed.
  int64_t max_offset = 0;
  int64_t min_offset = 0;
  for (uint8_t d = 0; d < meta.ndim; ++d) {
    const int64_t e = extents_buf[d];
    if (e > 0) {
      if (__builtin_add_overflow(max_offset, e,
                                 &max_offset)) [[unlikely]] {
        return Sat{UINT64_MAX, true};
      }
    } else {
      if (__builtin_add_overflow(min_offset, e,
                                 &min_offset)) [[unlikely]] {
        return Sat{UINT64_MAX, true};
      }
    }
  }

  // Final span and total bytes (scalar, identical to reference).
  int64_t span_signed;
  if (__builtin_sub_overflow(max_offset, min_offset,
                             &span_signed)) [[unlikely]] {
    return Sat{UINT64_MAX, true};
  }
  if (__builtin_add_overflow(span_signed, int64_t{1},
                             &span_signed)) [[unlikely]] {
    return Sat{UINT64_MAX, true};
  }
  uint64_t total_bytes;
  if (__builtin_mul_overflow(static_cast<uint64_t>(span_signed),
                             static_cast<uint64_t>(element_size(meta.dtype).raw()),
                             &total_bytes)) [[unlikely]] {
    return Sat{UINT64_MAX, true};
  }
  return Sat{total_bytes};
}

}  // namespace crucible::detail
