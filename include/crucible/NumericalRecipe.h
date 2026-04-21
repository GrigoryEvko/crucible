#pragma once

// ═══════════════════════════════════════════════════════════════════
// NumericalRecipe — the portability contract (FORGE.md §19)
//
// Per FORGE.md §19 and CRUCIBLE.md §10.5, every compiled kernel pins
// exactly one NumericalRecipe that dictates the algorithmic choices
// every downstream backend must honor exactly.  Same IR002 kernel +
// same recipe → numerically-equivalent results on any supported chip,
// bounded by the declared `ReductionDeterminism` tier.
//
// This header defines the types.  It is a STUB until the Forge
// compiler lands — the enums, struct, and RecipeHash are ready for
// the recipe registry (FORGE.md §20) and the cross-vendor numerics
// CI matrix (§20.4) to consume.
//
// The invariants declared here feed directly into:
//   - NumericalRecipe field on IR002 KernelNode (FORGE.md §18.2)
//   - Recipe registry JSON loader (FORGE.md §20.1)
//   - Fleet intersection picker at Phase E (FORGE.md §20.2)
//   - Per-backend realize_recipe() (MIMIC.md §40)
//   - Cross-vendor numerics CI harness (FORGE.md §20.4)
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/Types.h>

#include <bit>
#include <cstdint>
#include <utility>

namespace crucible {

// ─── Reduction algorithms ──────────────────────────────────────────
//
// How a kernel's accumulator steps across its K axis (or other
// reduction dimension).  Pairwise is the default; the other variants
// are reserved for specific recipes (KAHAN for extended-precision
// accumulation under BITEXACT_STRICT, BLOCK_STABLE for FP8 recipes
// with per-block scale application).
enum class ReductionAlgo : uint8_t {
  PAIRWISE,      // Standard pairwise tree; default for GEMM / attention
  LINEAR,        // Sequential left-fold; deterministic but slower
  KAHAN,         // Kahan compensated summation; rare, high-precision
  BLOCK_STABLE,  // Block-tree for MX/NVFP4 with explicit per-block scales
};

// ─── Rounding modes ────────────────────────────────────────────────
//
// IEEE 754 rounding mode.  RN (round-to-nearest-even) is the only
// mode currently enabled for production recipes; the others are
// reserved for specialized (e.g. deterministic-stochastic-rounding)
// future work.
enum class RoundingMode : uint8_t {
  RN,  // Round to nearest, ties to even (IEEE 754 default)
  RZ,  // Round toward zero (truncate)
  RM,  // Round toward minus infinity (floor)
  RP,  // Round toward plus infinity (ceiling)
};

// ─── Scale policy (for block-scaled formats) ───────────────────────
//
// FP8 MX, FP4 MX, and NVFP4 use scales applied at different granularity
// levels.  Scale policy is part of the recipe because its choice
// affects where the scale is applied in the accumulator pipeline,
// which has numerical consequences (cross-vendor divergence in the
// MX/NVFP4 case — see FORGE.md §19.1 note on non-BITEXACT availability
// for block-scaled formats).
enum class ScalePolicy : uint8_t {
  NONE,                // No scale applied (FP32 / BF16 / FP16 recipes)
  PER_TENSOR_POST,     // One scalar scale applied to the final output
  PER_TENSOR_PRE,      // One scalar scale applied to inputs pre-MMA
  PER_BLOCK_MX,        // Per-block MX scale (32-element blocks)
  PER_BLOCK_NVFP4,     // Per-block NVFP4 scale (16-element blocks)
  PER_CHANNEL,         // Per-output-channel scale (int8 quantization)
};

// ─── Softmax recurrence variant ────────────────────────────────────
//
// Attention's softmax accumulator can be computed in multiple ways,
// each producing slightly different bit patterns under FP16/BF16
// due to intermediate rounding.  The recipe pins which variant every
// backend must realize.  ONLINE_LSE (Rabe & Staats 2021) is the
// default — FlashAttention-1 style; FLASH2 and FLASH3 are more
// aggressive pipelinings that Mimic-NV can realize natively on
// Hopper+.
enum class SoftmaxRecurrence : uint8_t {
  NAIVE,        // Two-pass: max-subtract, exp, normalize
  ONLINE_LSE,   // Single-pass log-sum-exp (FlashAttention-1)
  FLASH2,       // FlashAttention-2 online softmax
  FLASH3,       // FlashAttention-3 warp-specialized online softmax
};

// ─── Determinism tiers (FORGE.md §19.1 / CRUCIBLE.md §10.5) ────────
//
// The four-tier escalation ladder; each tier is strictly more
// deterministic than the one below, with a perf tax proportional to
// its strictness.
//
//   UNORDERED          — no guarantee; fastest; typically ≤100 ULP
//                        cross-vendor in practice.  Not for training.
//   ORDERED            — reduction topology pinned (canonical index
//                        order); tile shapes free.  ≤4 ULP cross-
//                        vendor on FP16/BF16 recipes.  Default for
//                        mixed-vendor training; ~3-5% tax.
//   BITEXACT_TC        — K≤8 tensor-core fragments + pinned outer
//                        scalar reduction.  0-1 ULP cross-vendor.
//                        ~5-8% tax.  Pragmatic sweet spot for cross-
//                        vendor training with tensor-core perf.
//   BITEXACT_STRICT    — scalar FMA chain throughout (no tensor
//                        cores).  0 ULP byte-identical cross-vendor
//                        INCLUDING cross-architecture (NV / AM / TPU /
//                        TRN / CPU).  10-50× slower.  Reserved for
//                        CPU oracle, compliance, regression CI.
//
// Recipes using block-scaled formats (FP8 MX, FP4 MX) cannot declare
// BITEXACT_* — scale-application divergence exceeds software
// correction.  Highest available tier for those is ORDERED with
// per-recipe `tolerance_ulp_cross_vendor`.
enum class ReductionDeterminism : uint8_t {
  UNORDERED,
  ORDERED,
  BITEXACT_TC,
  BITEXACT_STRICT,
};

// ─── NumericalRecipe — the 16-byte pinned contract ─────────────────
//
// Every IR002 KernelNode references an interned `const NumericalRecipe*`.
// Two kernels with identical structural attrs and the same recipe
// pointer produce identical KernelContentHash values and therefore
// dedupe at every cache level.
//
// Layout: 1B×7 fields + 1B flags + 8B hash = 16B, cache-line-aligned.
struct alignas(16) NumericalRecipe {
  ScalarType           accum_dtype    = ScalarType::Float;              // 1B
  ScalarType           out_dtype      = ScalarType::Undefined;          // 1B
  ReductionAlgo        reduction_algo = ReductionAlgo::PAIRWISE;        // 1B
  RoundingMode         rounding       = RoundingMode::RN;               // 1B
  ScalePolicy          scale_policy   = ScalePolicy::NONE;              // 1B
  SoftmaxRecurrence    softmax        = SoftmaxRecurrence::ONLINE_LSE;  // 1B
  ReductionDeterminism determinism    = ReductionDeterminism::ORDERED;  // 1B
  // flags: bit 0 = flush_to_zero, bit 1 = split_k_atomic_ok,
  //        bit 2 = allow_denormal, bit 3 = attn_mask_add_in_fp32,
  //        bits 4-7 reserved.
  uint8_t              flags          = 0;                              // 1B
  // hash is Family-A (persistent) per Types.h taxonomy.  Computed at
  // intern time in the RecipePool.  Drives Phase E recipe picker,
  // kernel CSE, and L1 cache keys.
  RecipeHash           hash;                                            // 8B
};
static_assert(sizeof(NumericalRecipe) == 16,
              "NumericalRecipe must stay 16B — layout is load-bearing "
              "for the RecipePool Swiss table and KernelNode::recipe "
              "cache-line packing (see FORGE.md §19.1).");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(NumericalRecipe);

// ─── Hash computation (Family-A per Types.h taxonomy) ──────────────
//
// compute_recipe_hash folds the 8 semantic bytes of a recipe (seven
// 1-byte enum fields + the flags byte) through a MurmurHash3 finalizer.
// Deliberately EXCLUDES the `hash` field itself so that:
//
//   1. Re-hashing an already-populated recipe produces the same value
//      as hashing a fresh-constructed one with the same semantic
//      fields (idempotence).  Without this, deserializing a recipe
//      from disk and re-hashing it would drift.
//
//   2. Two recipes differ in their `hash` field values only when they
//      differ in semantic fields — never in the other direction.
//      Hash equality ⟺ semantic equality, modulo fmix64 collisions
//      (≈ 2^-64 per pair).
//
// Family-A properties honored:
//   - Cross-process stable: no pointer entropy, no ASLR dependence,
//     no endian-dependent bit reinterpretation.
//   - Wire-safe: hash composes into KernelContentHash (FORGE.md §18.6)
//     and L1 federation cache keys (§23.2); byte-stable for the
//     lifetime of the NumericalRecipe layout.
//   - Platform-independent: std::bit_cast on 1-byte trivially-copyable
//     enums is well-defined on every supported ABI; shifts and XORs
//     are integer ops with defined semantics.
//
// Cost: ~5 integer ops + one 64-bit multiply chain.  constexpr — can
// be computed at compile time for static starter recipes.

namespace detail_recipe {
    // MurmurHash3 64-bit finalizer.  Same constants as detail::fmix64
    // in Expr.h; duplicated here so NumericalRecipe.h stays free of
    // Expr.h's transitive include cost (Ops.h, Types.h was already
    // pulled).  Both finalizers produce identical bits given identical
    // inputs — verified by a cross-check test.
    [[nodiscard, gnu::const]] constexpr uint64_t fmix64(uint64_t k) noexcept {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        k *= 0xc4ceb9fe1a85ec53ULL;
        k ^= k >> 33;
        return k;
    }

    // Extract the raw byte of a 1-byte scoped enum without sign
    // extension.  ScalarType has Undefined = -1 (int8_t); casting
    // directly to uint64_t would sign-extend to 0xFF...FF and clobber
    // neighboring fields in the packed hash.  bit_cast to uint8_t
    // first preserves the bit pattern (Undefined → 0xFF in one byte).
    template <typename E>
    [[nodiscard, gnu::const]] constexpr uint8_t enum_byte(E e) noexcept {
        static_assert(sizeof(E) == 1,
                      "enum_byte expects a 1-byte scoped enum; widen the "
                      "hash fold if NumericalRecipe gains a multi-byte "
                      "enum field");
        return std::bit_cast<uint8_t>(e);
    }
}  // namespace detail_recipe

[[nodiscard, gnu::pure]] constexpr RecipeHash compute_recipe_hash(
    const NumericalRecipe& r) noexcept
{
    using detail_recipe::enum_byte;
    const uint64_t packed =
          (uint64_t(enum_byte(r.accum_dtype))    <<  0)
        | (uint64_t(enum_byte(r.out_dtype))      <<  8)
        | (uint64_t(enum_byte(r.reduction_algo)) << 16)
        | (uint64_t(enum_byte(r.rounding))       << 24)
        | (uint64_t(enum_byte(r.scale_policy))   << 32)
        | (uint64_t(enum_byte(r.softmax))        << 40)
        | (uint64_t(enum_byte(r.determinism))    << 48)
        | (uint64_t(r.flags)                     << 56);
    return RecipeHash{detail_recipe::fmix64(packed)};
}

// Return a copy of the recipe with its `hash` field populated from
// compute_recipe_hash.  Idempotent: hashing an already-hashed recipe
// produces the same value.  Useful at construction sites that want
// a fully-populated recipe without threading the hash computation
// through the caller.
[[nodiscard, gnu::pure]] constexpr NumericalRecipe hashed(
    NumericalRecipe r) noexcept
{
    r.hash = compute_recipe_hash(r);
    return r;
}

// ─── Utility predicates (gnu::const — pure function of one arg) ────

[[nodiscard, gnu::const]] constexpr bool is_bitexact(
    ReductionDeterminism d) noexcept
{
  return d == ReductionDeterminism::BITEXACT_TC
      || d == ReductionDeterminism::BITEXACT_STRICT;
}

[[nodiscard, gnu::const]] constexpr bool permits_tensor_cores(
    ReductionDeterminism d) noexcept
{
  // BITEXACT_STRICT forbids tensor cores entirely (scalar FMA only).
  // All other tiers permit them, though BITEXACT_TC constrains the
  // K dimension (≤8) and outer reduction order.
  return d != ReductionDeterminism::BITEXACT_STRICT;
}

[[nodiscard, gnu::const]] constexpr bool allows_block_scaled_formats(
    ReductionDeterminism d) noexcept
{
  // Block-scaled formats (FP8 MX, FP4 MX) diverge across vendors by
  // more than 1 ULP due to scale-application differences, so
  // BITEXACT_* tiers cannot apply.  ORDERED is the strongest
  // available for recipes using PER_BLOCK_* scale policies.
  return d == ReductionDeterminism::UNORDERED
      || d == ReductionDeterminism::ORDERED;
}

} // namespace crucible
