#pragma once

// ═══════════════════════════════════════════════════════════════════
// RecipeRegistry — named starter recipes (FORGE.md §20).
//
// Every training run picks a recipe by name from this registry (user-
// facing API), or by policy-driven fleet intersection at Phase E
// RecipeSelect (compiler-internal).  The registry is the single source
// of truth for "which NumericalRecipe goes with the name f16_f32accum_tc".
//
// Scope of this Layer-3 header:
//   - An 8-recipe STARTER set spanning the four determinism tiers and
//     the common FP32 / FP16 / BF16 / FP8 dtype combinations.  These
//     are the recipes a bootstrap Forge must be able to pick without
//     any external configuration (FORGE.md §20.1 recipes.json "starter
//     registry" corner of the full ~40-recipe production catalog).
//   - A by_name lookup returning the interned canonical pointer.
//   - A flat entries() enumeration for diagnostics / CI.
//
// DEFERRED to when dependent subsystems land:
//   - The JSON loader for crucible/data/recipes.json (FORGE.md §20.1)
//     — additive, shares the same by_name surface.  Waits on a
//     JSON-parser choice and a concrete recipes.json file.
//   - The native_on chip bitmap + fleet-intersection picker (§20.2–20.3)
//     — requires Mimic ChipId and Canopy RelayAnnouncement types which
//     do not exist yet.  Users pin by_name until then.
//   - Phase-E RecipeSelect integration — requires IR002 KernelNode
//     which does not exist yet.
//
// The scope here is precisely the tractable surface: pure-data starter
// recipes interned into a RecipePool, with a by_name map.  Zero
// coupling to absent subsystems; drop-in replacement target for the
// JSON loader when Mimic/Canopy lands.
//
// ─── Safety ─────────────────────────────────────────────────────────
//
//   InitSafe    — starter table is consteval; construction populates
//                 every entry via pool.intern() in a bounded loop.
//   TypeSafe    — by_name returns std::expected<const NumericalRecipe*,
//                 RecipeError>; raw name lookup cannot silently coerce.
//   NullSafe    — only populated entries are exposed; entries() span
//                 is over fully-initialized storage; by_name hit path
//                 returns a guaranteed-non-null recipe pointer.
//   MemSafe     — holds pointers into the caller's pool→arena; copy
//                 and move deleted with reasons.
//   BorrowSafe  — single-threaded init by convention; read-only after
//                 construction (every caller of by_name races only
//                 against the other readers, never the writer).
//   ThreadSafe  — read-only-after-init is the documented contract.
//   LeakSafe    — arena bulk-frees the pool; registry owns no heap.
//   DetSafe     — by_name depends only on name+this; starter table is
//                 deterministic; pool intern is deterministic given
//                 the call sequence (Layer 2).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Effects.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Platform.h>
#include <crucible/RecipePool.h>
#include <crucible/Types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace crucible {

// ─── Error taxonomy ─────────────────────────────────────────────────
//
// by_name is the only path that can fail against user input; a bad
// recipe name is the sole error class.  The enum is stable across
// versions — new recipes are added by appending, not renumbering.
enum class RecipeError : uint8_t {
  NameNotFound = 1,
};

class CRUCIBLE_OWNER RecipeRegistry {
 public:
  // Registry-local (name, recipe*) binding.  name points at a static
  // string literal (the starter table lives in the header's .rodata);
  // recipe* points into the RecipePool's arena.  Both lifetimes
  // outlive every realistic caller; the registry itself encodes this
  // with CRUCIBLE_LIFETIMEBOUND on entries().
  struct Entry {
    std::string_view name;
    const NumericalRecipe* recipe = nullptr;
  };

  // Fixed starter-set size.  New starter recipes bump this; tests
  // assert entries().size() == STARTER_COUNT so a forgotten update
  // fires immediately.
  static constexpr std::size_t STARTER_COUNT = 8;

  // Populate the registry by interning every starter recipe into the
  // caller-supplied pool.  Each pool.intern call is a fresh allocation
  // (no pool populated yet) so this runs in O(STARTER_COUNT × probe)
  // = O(1) in practice.
  //
  // The pool is caller-owned; the registry holds a non-owning pointer
  // to the pool's arena via the interned recipe pointers, but never
  // mutates or destroys the pool.  Pool outlives the registry — the
  // lifetime contract is implicit in the construction order.
  [[gnu::cold]] explicit RecipeRegistry(RecipePool& pool CRUCIBLE_LIFETIMEBOUND,
                                        fx::Alloc a) noexcept;

  // Interior pointers into pool_→arena; relocating would dangle.
  RecipeRegistry(const RecipeRegistry&)            = delete("RecipeRegistry holds interior pointers into the caller's pool");
  RecipeRegistry& operator=(const RecipeRegistry&) = delete("RecipeRegistry holds interior pointers into the caller's pool");
  RecipeRegistry(RecipeRegistry&&)                 = delete("interior pointers would dangle");
  RecipeRegistry& operator=(RecipeRegistry&&)      = delete("interior pointers would dangle");

  // Look up a starter recipe by name.  Returns the canonical interned
  // pointer on hit, RecipeError::NameNotFound on miss.
  //
  // Comparison is case-sensitive and exact.  The lookup is a linear
  // scan over the tiny starter table — at 8 entries × 24 B per Entry
  // = 192 B, two cache lines, the scan is faster than any hash table
  // would be.
  //
  // Cost: ~15 ns for a miss, ~5-10 ns for a hit.
  [[nodiscard, gnu::pure]] std::expected<const NumericalRecipe*, RecipeError>
      by_name(std::string_view name) const noexcept;

  // Enumerate every (name, recipe*) binding.  Order matches the
  // starter table declaration order in the .cpp; stable across the
  // lifetime of the registry.
  //
  // Used by tests (to sweep every starter recipe), Meridian probes
  // (to emit native_on bitmaps per chip — future), and diagnostic
  // dumps (`crucible-top --recipes`).
  [[nodiscard, gnu::pure]] std::span<const Entry> entries() const noexcept
      CRUCIBLE_LIFETIMEBOUND
  {
    return {entries_.data(), STARTER_COUNT};
  }

  // Convenience: the count of starter recipes, independent of the
  // array being populated.  Used in static_asserts on generated
  // tables and CI.
  [[nodiscard, gnu::const]] static constexpr std::size_t size() noexcept {
    return STARTER_COUNT;
  }

 private:
  std::array<Entry, STARTER_COUNT> entries_{};
};

// ─── Starter recipe names ──────────────────────────────────────────
//
// Exposed as inline constexpr string_views so callers can pin by
// constant rather than magic strings.  Renaming any of these is a
// wire-format break across every Cipher entry that pinned the recipe
// by name; the names are frozen at CDAG_VERSION boundaries.
//
// Naming convention (FORGE.md §20.1): <storage-dtype>_<accum-qualifier>_<tier-suffix>.
//   - f32_strict      — FP32 end-to-end, BITEXACT_STRICT
//   - f32_ordered     — FP32 end-to-end, ORDERED
//   - f16_f32accum_*  — FP16 storage, FP32 accumulator, <tier>
//   - bf16_f32accum_* — BF16 storage, FP32 accumulator, <tier>
//   - fp8e4m3_f32accum_mx_ordered — FP8-E4M3 storage, FP32 accum,
//                                    per-block MX scales, ORDERED
//   - fp8e5m2_f32accum_mx_ordered — FP8-E5M2 storage, FP32 accum,
//                                    per-block MX scales, ORDERED

namespace recipe_names {
inline constexpr std::string_view kF32Strict             = "f32_strict";
inline constexpr std::string_view kF32Ordered            = "f32_ordered";
inline constexpr std::string_view kF16F32AccumTc         = "f16_f32accum_tc";
inline constexpr std::string_view kF16F32AccumOrdered    = "f16_f32accum_ordered";
inline constexpr std::string_view kBf16F32AccumTc        = "bf16_f32accum_tc";
inline constexpr std::string_view kBf16F32AccumOrdered   = "bf16_f32accum_ordered";
inline constexpr std::string_view kFp8E4m3F32AccumMxOrd  = "fp8e4m3_f32accum_mx_ordered";
inline constexpr std::string_view kFp8E5m2F32AccumMxOrd  = "fp8e5m2_f32accum_mx_ordered";
}  // namespace recipe_names

// ─── Starter recipe specs (the pure-data source of truth) ──────────
//
// Every starter entry is a (name, semantic fields) pair.  The
// semantic fields feed through compute_recipe_hash → RecipePool at
// registry construction time.  These specs are constexpr so they
// live in .rodata and are guaranteed not to drift across TU boundaries.

namespace detail_recipe_registry {

struct StarterSpec {
  std::string_view name;
  NumericalRecipe  fields;  // `hash` populated via hashed() below
};

// Eight starter recipes — the tractable cross-section of the four-
// tier determinism × {FP32, FP16, BF16, FP8} matrix.  Every row has
// been hand-verified against FORGE.md §20.1; a new row bumps
// RecipeRegistry::STARTER_COUNT AND adds an entry to the by_name
// switch in RecipeRegistry.cpp-equivalent path (inline below).
inline constexpr std::array<StarterSpec, RecipeRegistry::STARTER_COUNT>
    kStarterRecipes = {{
        // 1. f32_strict — FP32 end-to-end, bit-identical on any
        //    silicon including CPU oracle.  The compliance / replay
        //    reference.  10-50× slower than UNORDERED but 0 ULP on
        //    every backend.
        {recipe_names::kF32Strict, hashed(NumericalRecipe{
            .accum_dtype    = ScalarType::Float,
            .out_dtype      = ScalarType::Float,
            .reduction_algo = ReductionAlgo::PAIRWISE,
            .rounding       = RoundingMode::RN,
            .scale_policy   = ScalePolicy::NONE,
            .softmax        = SoftmaxRecurrence::ONLINE_LSE,
            .determinism    = ReductionDeterminism::BITEXACT_STRICT,
            .flags          = 0,
            .hash           = {},
        })},
        // 2. f32_ordered — FP32 with vendor-native tile shapes;
        //    ≤4 ULP cross-vendor.  The default for FP32 training on
        //    a heterogeneous fleet.
        {recipe_names::kF32Ordered, hashed(NumericalRecipe{
            .accum_dtype    = ScalarType::Float,
            .out_dtype      = ScalarType::Float,
            .reduction_algo = ReductionAlgo::PAIRWISE,
            .rounding       = RoundingMode::RN,
            .scale_policy   = ScalePolicy::NONE,
            .softmax        = SoftmaxRecurrence::ONLINE_LSE,
            .determinism    = ReductionDeterminism::ORDERED,
            .flags          = 0,
            .hash           = {},
        })},
        // 3. f16_f32accum_tc — FP16 storage, FP32 accumulator,
        //    K≤8 tensor-core fragments + pinned outer scalar reduction.
        //    0-1 ULP cross-vendor; ~5-8% tax vs UNORDERED.  The
        //    pragmatic sweet spot for cross-vendor mixed-precision
        //    training with tensor-core throughput.
        {recipe_names::kF16F32AccumTc, hashed(NumericalRecipe{
            .accum_dtype    = ScalarType::Float,
            .out_dtype      = ScalarType::Half,
            .reduction_algo = ReductionAlgo::PAIRWISE,
            .rounding       = RoundingMode::RN,
            .scale_policy   = ScalePolicy::NONE,
            .softmax        = SoftmaxRecurrence::ONLINE_LSE,
            .determinism    = ReductionDeterminism::BITEXACT_TC,
            .flags          = 0,
            .hash           = {},
        })},
        // 4. f16_f32accum_ordered — FP16 storage, FP32 accumulator,
        //    vendor-native tile shapes; ≤4 ULP cross-vendor.  Default
        //    for heterogeneous-fleet FP16 training when BITEXACT_TC
        //    isn't required.
        {recipe_names::kF16F32AccumOrdered, hashed(NumericalRecipe{
            .accum_dtype    = ScalarType::Float,
            .out_dtype      = ScalarType::Half,
            .reduction_algo = ReductionAlgo::PAIRWISE,
            .rounding       = RoundingMode::RN,
            .scale_policy   = ScalePolicy::NONE,
            .softmax        = SoftmaxRecurrence::ONLINE_LSE,
            .determinism    = ReductionDeterminism::ORDERED,
            .flags          = 0,
            .hash           = {},
        })},
        // 5. bf16_f32accum_tc — BF16 storage, FP32 accumulator,
        //    K≤8 tensor-core fragments.  BF16 is the training-default
        //    on modern silicon; this recipe is what most TC training
        //    runs should pin.
        {recipe_names::kBf16F32AccumTc, hashed(NumericalRecipe{
            .accum_dtype    = ScalarType::Float,
            .out_dtype      = ScalarType::BFloat16,
            .reduction_algo = ReductionAlgo::PAIRWISE,
            .rounding       = RoundingMode::RN,
            .scale_policy   = ScalePolicy::NONE,
            .softmax        = SoftmaxRecurrence::ONLINE_LSE,
            .determinism    = ReductionDeterminism::BITEXACT_TC,
            .flags          = 0,
            .hash           = {},
        })},
        // 6. bf16_f32accum_ordered — BF16 storage, FP32 accumulator,
        //    vendor-native tile shapes.  Tolerance-bounded training
        //    on heterogeneous fleets.
        {recipe_names::kBf16F32AccumOrdered, hashed(NumericalRecipe{
            .accum_dtype    = ScalarType::Float,
            .out_dtype      = ScalarType::BFloat16,
            .reduction_algo = ReductionAlgo::PAIRWISE,
            .rounding       = RoundingMode::RN,
            .scale_policy   = ScalePolicy::NONE,
            .softmax        = SoftmaxRecurrence::ONLINE_LSE,
            .determinism    = ReductionDeterminism::ORDERED,
            .flags          = 0,
            .hash           = {},
        })},
        // 7. fp8e4m3_f32accum_mx_ordered — FP8-E4M3 storage, FP32
        //    accumulator, per-block MX scales.  Softmax NAIVE because
        //    block-scaled formats don't compose cleanly with online
        //    LSE; softmax is implemented as two-pass max-sub/exp/norm
        //    in the IR003* realization.  Cannot declare BITEXACT_*
        //    per FORGE.md §19.1 (block-scale divergence > 1 ULP).
        {recipe_names::kFp8E4m3F32AccumMxOrd, hashed(NumericalRecipe{
            .accum_dtype    = ScalarType::Float,
            .out_dtype      = ScalarType::Float8_e4m3fn,
            .reduction_algo = ReductionAlgo::PAIRWISE,
            .rounding       = RoundingMode::RN,
            .scale_policy   = ScalePolicy::PER_BLOCK_MX,
            .softmax        = SoftmaxRecurrence::NAIVE,
            .determinism    = ReductionDeterminism::ORDERED,
            .flags          = 0,
            .hash           = {},
        })},
        // 8. fp8e5m2_f32accum_mx_ordered — FP8-E5M2 storage (5-bit
        //    exponent, larger dynamic range than E4M3), FP32 accum,
        //    per-block MX scales.  Paired with E4M3 for gradient/
        //    weight asymmetric training (gradients often use E5M2 for
        //    the dynamic range).
        {recipe_names::kFp8E5m2F32AccumMxOrd, hashed(NumericalRecipe{
            .accum_dtype    = ScalarType::Float,
            .out_dtype      = ScalarType::Float8_e5m2,
            .reduction_algo = ReductionAlgo::PAIRWISE,
            .rounding       = RoundingMode::RN,
            .scale_policy   = ScalePolicy::PER_BLOCK_MX,
            .softmax        = SoftmaxRecurrence::NAIVE,
            .determinism    = ReductionDeterminism::ORDERED,
            .flags          = 0,
            .hash           = {},
        })},
    }};

}  // namespace detail_recipe_registry

// ─── Inline implementation ──────────────────────────────────────────

inline RecipeRegistry::RecipeRegistry(RecipePool& pool CRUCIBLE_LIFETIMEBOUND,
                                      fx::Alloc a) noexcept
{
  // Intern every starter spec into the pool; the pool writes the
  // authoritative hash, we capture the canonical pointer.  Order
  // matches kStarterRecipes.
  for (std::size_t i = 0; i < STARTER_COUNT; ++i) {
    const auto& spec = detail_recipe_registry::kStarterRecipes[i];
    entries_[i].name   = spec.name;
    entries_[i].recipe = pool.intern(a, spec.fields);
  }
}

inline std::expected<const NumericalRecipe*, RecipeError>
RecipeRegistry::by_name(std::string_view name) const noexcept
{
  // Linear scan over ~8 entries; cache-friendly, no hash overhead.
  // Branch-light: every iteration is `if (name == entry.name) return`.
  for (const auto& e : entries_) {
    if (e.name == name) {
      return e.recipe;  // guaranteed non-null: populated in ctor
    }
  }
  return std::unexpected(RecipeError::NameNotFound);
}

}  // namespace crucible
