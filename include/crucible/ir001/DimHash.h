#pragma once

// ═══════════════════════════════════════════════════════════════════
// DimHash.h — shared SIMD dim-hash helper for TensorMeta
//
// Replaces the manually-inlined dim-hash chain at
// BackgroundThread.h:747-759 and MerkleDag.h:725-734.  Both call
// sites computed the per-tensor XOR-fold:
//
//   uint64_t dim_h = 0;
//   for (uint8_t d = 0; d < meta.ndim; ++d) {
//     dim_h ^= uint64_t(meta.sizes  [d]) * detail::kDimMix[d];
//     dim_h ^= uint64_t(meta.strides[d]) * detail::kDimMix[d + 8];
//   }
//
// Crucible's TensorMeta::sizes[8] + strides[8] (MerkleDag.h:55-56) is
// laid out for SIMD: 8 lanes × int64 = 64 bytes per array, naturally
// aligned, max ndim=8.  Perfect fit for one AVX-512 zmm or two AVX2
// ymm registers — same lane semantics either way.
//
// ─── DetSafe contract (CLAUDE.md §II.8) ─────────────────────────────
//
// dim_hash_simd MUST produce IDENTICAL bits to the scalar inlined
// chain on every supported ISA.  This is enforced by:
//
//   * XOR is associative and commutative for integer types — the
//     reduction order does not affect output.
//   * Per-lane multiplies are bit-exact (two's-complement sat).
//   * std::simd::vec<int64_t, 8> with fixed lane count pins lane[d]
//     to dimension d across every ISA.
//   * The lookup table kDimMix[16] uses two halves: lo for sizes,
//     hi for strides.  Two aligned loads + two multiplies preserve
//     the indexing scheme.
//
// ─── Performance ────────────────────────────────────────────────────
//
// Scalar baseline (Zen4 @ 4.7 GHz, ndim=8): ~15 ns/tensor.
//   8× int64 multiply (1 cy each, latency 3) + 16× xor (1 cy each)
//   serialised through the per-lane FMA dependency.
//
// SIMD target: ~3-5 ns/tensor.
//   2× SIMD multiply (one for sizes, one for strides) + 1 SIMD xor
//   + 1 masked horizontal xor-fold (std::simd::reduce with mask+id).
//   All independent — fits in ~8 cycles.
//
// Cost is bounded for ndim < 8 because the SIMD work is the same
// (8 lanes processed regardless), but lane-mask zeroing costs one
// extra instruction.  Below ndim=4 the scalar baseline may be
// competitive due to reduced instruction count; the SIMD helper
// remains correct.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/ir001/Expr.h>            // detail::kDimMix
#include <crucible/Platform.h>
#include <crucible/ir001/TensorMeta.h>      // TensorMeta (extracted from MerkleDag.h)
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/Simd.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <functional>   // std::bit_xor<>
#include <simd>
#include <type_traits>

namespace crucible {

// WRAP-DimHash-1 (#910): the per-TensorMeta dimension hash is a
// deterministic, process-local helper value.  It is safe to recompute
// from TensorMeta bytes, but it is not a persistent artifact key by
// itself.  The nested wrapper forces consumers to acknowledge both
// facts before mixing or exporting the bits.
using DimHash = ::crucible::safety::Tagged<
    uint64_t, ::crucible::hash_family::FamilyB>;
using DimHashDet = ::crucible::safety::DetSafe<
    ::crucible::safety::DetSafeTier_v::Pure, DimHash>;

static_assert(sizeof(DimHash) == sizeof(uint64_t),
    "Tagged<uint64_t, hash_family::FamilyB> must EBO-collapse so "
    "dim-hash stays register-sized");
static_assert(sizeof(DimHashDet) == sizeof(uint64_t),
    "DetSafe<Pure, Tagged<uint64_t, hash_family::FamilyB>> must "
    "EBO-collapse so dim-hash stays register-sized");
static_assert(std::is_trivially_copyable_v<DimHash>);
static_assert(std::is_trivially_copyable_v<DimHashDet>);
static_assert(std::is_standard_layout_v<DimHash>);
static_assert(std::is_standard_layout_v<DimHashDet>);

[[nodiscard]] inline constexpr DimHash
dim_hash(uint64_t hash) noexcept {
  return DimHash{hash};
}

[[nodiscard]] inline constexpr uint64_t
raw_dim_hash(const DimHash& hash) noexcept {
  return hash.value();
}

[[nodiscard]] inline constexpr uint64_t
raw_dim_hash(const DimHashDet& hash) noexcept {
  return raw_dim_hash(hash.peek());
}

}  // namespace crucible

namespace crucible::detail {

// XOR-fold per-dimension hash of a TensorMeta's (sizes, strides) pair.
// Equivalent to:
//   uint64_t result = 0;
//   for (uint8_t d = 0; d < meta.ndim; ++d) {
//     result ^= uint64_t(meta.sizes  [d]) * kDimMix[d];
//     result ^= uint64_t(meta.strides[d]) * kDimMix[d + 8];
//   }
//   return result;
//
// Pre: meta.ndim <= 8 (TensorMeta layout invariant; enforced by
// compute_storage_nbytes contract).
//
// gnu::pure: depends on caller-visible memory (the meta argument's
// arrays) but has no side effects and no global state.  Safe to CSE
// across multiple calls with the same input.

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
uint64_t dim_hash_simd(const TensorMeta& meta) noexcept {
  using simd::i64x8;
  using simd::u64x8;

  // Load full 8 lanes from each array.  TensorMeta::sizes[8] and
  // strides[8] are well-defined for the full lane range (zero-init
  // past ndim per InitSafe + NSDMI in the struct definition), so a
  // plain vector load is in-bounds even when meta.ndim < 8.
  //
  // std::simd::unchecked_load<V>(iter, count) uses element-aligned
  // loads.  TensorMeta itself is only naturally aligned in vectors and
  // trace-loader buffers, so vector-aligned loads would be unsound even
  // though the arrays are 64 bytes wide.
  auto sizes   = std::simd::unchecked_load<i64x8>(
      meta.sizes.raw_data(),   i64x8::size());
  auto strides = std::simd::unchecked_load<i64x8>(
      meta.strides.raw_data(), i64x8::size());

  // Load kDimMix halves: kDimMix[0..7] for sizes, kDimMix[8..15] for
  // strides.  element_aligned (== default) because the table is a
  // constexpr global with only alignof(uint64_t) guarantee.
  auto mix_lo = std::simd::unchecked_load<u64x8>(
      detail::kDimMix,     u64x8::size());
  auto mix_hi = std::simd::unchecked_load<u64x8>(
      detail::kDimMix + 8, u64x8::size());

  // Cast sizes/strides from int64 → uint64 to match the scalar
  // uint64_t arithmetic.  std::simd::vec has a converting
  // constructor between same-width integers; zero machine cost
  // (vreinterpret on every ISA).
  u64x8 sizes_u  (sizes);
  u64x8 strides_u(strides);

  // Per-lane multiplies and XOR-combine.
  u64x8 combined = (sizes_u * mix_lo) ^ (strides_u * mix_hi);

  // Masked horizontal XOR-fold: reduce only lanes [0, ndim), using
  // identity element 0 for masked-out lanes.  std::simd::reduce's
  // masked overload folds this into one call — no explicit select
  // + temporary vector materialization.
  auto valid_mask = simd::prefix_mask<u64x8>(static_cast<int>(meta.ndim));
  return std::simd::reduce(combined, valid_mask, std::bit_xor<>{}, 0ULL);
}

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
DimHashDet dim_hash_simd_det(const TensorMeta& meta) noexcept {
  return DimHashDet{dim_hash(dim_hash_simd(meta))};
}

// Scalar reference implementation — kept exposed so equivalence
// fuzzers can compare scalar vs SIMD output bit-for-bit, and so
// performance bench harnesses have a baseline to measure against.
//
// Identical to the inlined chain at BackgroundThread.h:747-752 and
// MerkleDag.h:725-728.  Any future change to the dim-hash algorithm
// MUST update both this function AND dim_hash_simd in lockstep,
// then update the equivalence fuzzer.

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
uint64_t dim_hash_scalar(const TensorMeta& meta) noexcept {
  uint64_t result = 0;
  for (uint8_t d = 0; d < meta.ndim; ++d) {
    result ^= static_cast<uint64_t>(raw_tensor_dim(meta.sizes[d])) *
              detail::kDimMix[d];
    result ^= static_cast<uint64_t>(raw_tensor_dim(meta.strides[d])) *
              detail::kDimMix[d + 8];
  }
  return result;
}

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE
DimHashDet dim_hash_scalar_det(const TensorMeta& meta) noexcept {
  return DimHashDet{dim_hash(dim_hash_scalar(meta))};
}

}  // namespace crucible::detail
