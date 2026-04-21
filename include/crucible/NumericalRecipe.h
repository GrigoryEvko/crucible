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

#include <cstdint>

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
