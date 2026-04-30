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

#include <crucible/effects/Capabilities.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Platform.h>
#include <crucible/RecipePool.h>
#include <crucible/Types.h>
#include <crucible/safety/NumericalTier.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace crucible {

// ─── Error taxonomy ─────────────────────────────────────────────────
//
// Three distinct miss conditions:
//   - NameNotFound — user or pin referenced an unknown recipe name.
//     Recoverable: fall back to a default; log; etc.
//   - HashNotFound — Cipher loaded a persisted RecipeHash that the
//     current-process registry cannot resolve.  Indicates either a
//     registry downgrade (the hash was persisted by a newer build
//     that knew a recipe this build doesn't) or file corruption.
//   - ToleranceMismatch — by_name_pinned/by_hash_pinned (FOUND-G04)
//     found the recipe but its tolerance class does not satisfy the
//     pinned admission tier.  Captures the load-bearing
//     "BITEXACT_STRICT consumer accidentally invokes UNORDERED
//     reduction" / "ULP_FP16-class consumer admits an ORDERED 4-ULP
//     recipe" bug class at the registry-lookup boundary instead of
//     in the cross-vendor numerics CI 12 hours later.  Ordinal 3
//     appended at the END so existing serialized blobs that pinned
//     by NameNotFound (=1) / HashNotFound (=2) keep their meaning.
//
// Enum ordinals are stable across versions — new recipes are added by
// appending entries to the registry, not by renumbering the errors.
enum class RecipeError : uint8_t {
  NameNotFound      = 1,
  HashNotFound      = 2,
  ToleranceMismatch = 3,
};

// ─── tolerance_for_dtype — output-precision → Tolerance tier mapping ─
//
// Static helper.  Used by tolerance_of() below to map a recipe's
// out_dtype + determinism into the canonical safety::Tolerance class.
// Exposed at namespace scope so call sites verifying the mapping
// against an external recipe file can re-use it.
//
// The mapping is INTENTIONALLY conservative — every entry is the
// strongest tier the dtype's 1-ULP error budget can sustain.  Coarser
// budgets (e.g. ORDERED's ≤4 ULP at FP16) MUST NOT collapse into the
// per-dtype 1-ULP class — that would defeat the load-bearing
// admission-gate rejection.
[[nodiscard, gnu::const]] constexpr safety::Tolerance
tolerance_for_dtype(ScalarType dtype) noexcept {
  switch (dtype) {
    case ScalarType::Double:        return safety::Tolerance::ULP_FP64;
    case ScalarType::Float:         return safety::Tolerance::ULP_FP32;
    case ScalarType::Half:          return safety::Tolerance::ULP_FP16;
    case ScalarType::BFloat16:      return safety::Tolerance::ULP_FP16;
    case ScalarType::Float8_e4m3fn: return safety::Tolerance::ULP_FP8;
    case ScalarType::Float8_e5m2:   return safety::Tolerance::ULP_FP8;
    case ScalarType::Char:
    case ScalarType::Byte:          return safety::Tolerance::ULP_INT8;
    default:                        return safety::Tolerance::RELAXED;
  }
}

// ─── tolerance_of — recipe → satisfied tolerance class ──────────────
//
// Maps the (determinism, out_dtype) pair to the strongest Tolerance
// tier the recipe is admitted to claim.  Invariant:
//
//   tolerance_of(r) == BITEXACT  ⟺  determinism == BITEXACT_STRICT.
//   tolerance_of(r) == ULP_FP*   ⟺  determinism == BITEXACT_TC
//                                    (per the storage dtype's 1-ULP class).
//   tolerance_of(r) == RELAXED   ⟺  determinism ∈ {ORDERED, UNORDERED}
//                                    (ORDERED's ≤4 ULP cross-vendor is
//                                    coarser than the lattice's finest
//                                    ULP_* tier — strict mapping).
//
// Why ORDERED → RELAXED, not ULP_FP*: ORDERED guarantees ≤4 ULP at
// the storage dtype, but our 7-tier Tolerance lattice's ULP_FP*
// classes denote 1-ULP-at-precision.  Mapping ORDERED to ULP_FP16
// for an FP16 recipe would let a 4-ULP recipe silently flow into a
// consumer asking for 1-ULP-tolerance — the load-bearing bug class
// the wrapper exists to prevent.  Conservative is correct.
[[nodiscard, gnu::const]] constexpr safety::Tolerance
tolerance_of(NumericalRecipe const& r) noexcept {
  switch (r.determinism) {
    case ReductionDeterminism::BITEXACT_STRICT:
      // 0 ULP byte-identical across every supported chip.  The only
      // recipe class that can claim BITEXACT.
      return safety::Tolerance::BITEXACT;
    case ReductionDeterminism::BITEXACT_TC:
      // 0-1 ULP cross-vendor at TC fragment level; admit at the
      // out_dtype's 1-ULP class.  Strictly stronger than ORDERED but
      // not bit-identical (the "0-1" includes the case where it's 1).
      return tolerance_for_dtype(r.out_dtype);
    case ReductionDeterminism::ORDERED:
    case ReductionDeterminism::UNORDERED:
      // ≤4 ULP (ORDERED) or unbounded (UNORDERED); coarser than any
      // 1-ULP class.  Conservative: admit only at RELAXED (lattice
      // bottom).  See header comment for rationale.
      return safety::Tolerance::RELAXED;
    default:
      // Defensive: -Werror=switch-default requires this arm even
      // though the four-tier enum is exhaustive above.  Future
      // ReductionDeterminism additions land here as RELAXED until
      // their tolerance class is explicitly mapped.
      return safety::Tolerance::RELAXED;
  }
}

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
                                        effects::Alloc a) noexcept;

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

  // Look up a starter recipe by Family-A hash.
  //
  // This is the Cipher load path: a persisted KernelContentHash
  // (FORGE.md §18.6) composes a RecipeHash; on recovery the Cipher
  // hands the hash to the registry to resolve back to the canonical
  // const NumericalRecipe* pointer in the current process.
  //
  // Hash mismatch modes:
  //   - The persisted registry had a recipe this process's registry
  //     doesn't (registry downgrade): returns HashNotFound.
  //   - The persistence blob was corrupted: same, returns HashNotFound.
  //   - The Family-A hash fold changed since persist (CDAG_VERSION
  //     break): same, returns HashNotFound.
  //
  // Callers MUST handle the miss — falling back to a default recipe
  // is usually wrong (it breaks the load-bearing replay-determinism
  // invariant from CRUCIBLE.md §10).  The right escalation is: abort
  // the load, surface a "recipe downgrade" diagnostic, require the
  // operator to either re-run with the newer build or accept the
  // non-replayable loss.
  //
  // Cost: ~8 ns miss, ~5 ns hit — one predicated branch per entry,
  // no string compare overhead.  Compared to by_name this is tighter
  // because 8-byte hash compare is a single integer test.
  [[nodiscard, gnu::pure]] std::expected<const NumericalRecipe*, RecipeError>
      by_hash(RecipeHash hash) const noexcept;

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

  // ═══════════════════════════════════════════════════════════════
  // FOUND-G04: NumericalTier-pinned recipe lookup
  // ═══════════════════════════════════════════════════════════════
  //
  // Type-pinned overlay for by_name / by_hash that lifts the
  // recipe's runtime tolerance class into the type system at the
  // boundary.  A consumer pinned at, e.g., `safety::Tolerance::
  // BITEXACT` can ONLY obtain a `NumericalTier<BITEXACT, const
  // NumericalRecipe*>` from the registry — registry returns
  // ToleranceMismatch for any recipe whose `tolerance_of(*r)` does
  // not subsume the requested static tier.
  //
  // The bug class caught: a refactor that pins a hot-path recipe
  // consumer to ULP_FP16 but accidentally accepts a recipe with
  // `ReductionDeterminism::ORDERED` (≤4 ULP, NOT ≤1 ULP at FP16).
  // Today caught by cross-vendor numerics CI 12 hours after the
  // commit lands; with the pinned overload, caught at the registry
  // boundary the moment the recipe is pulled.
  //
  // Subsumption semantics (per ToleranceLattice):
  //
  //   Bottom = RELAXED (loosest); Top = BITEXACT (tightest).
  //   leq(loose, tight) reads "loose is below tight."  A producer
  //   at HIGHER tier (BITEXACT) satisfies a consumer at LOWER tier
  //   (ULP_FP16) — stronger promise serves weaker requirement.
  //
  //   So: `by_name_pinned<RELAXED>("any_recipe")` always succeeds
  //   (every recipe satisfies RELAXED).
  //
  //   And:  `by_name_pinned<BITEXACT>("f32_strict")` succeeds.
  //         `by_name_pinned<BITEXACT>("f32_ordered")` returns
  //         ToleranceMismatch — ORDERED maps to RELAXED, which
  //         does NOT satisfy BITEXACT.
  //
  // Error priority: NameNotFound / HashNotFound take precedence
  // over ToleranceMismatch.  An unknown name surfaces NameNotFound
  // even if the static tier is RELAXED — symmetric with by_name's
  // contract; pinned is purely additive.
  //
  // Cost: ~5-10 ns hit (linear scan + tolerance_of switch + leq
  // compare); ~15 ns miss.  No heap, no atomic, no CAS — same as
  // the non-pinned variant.

  // ── by_name_pinned — name lookup with tier admission ────────────
  template <safety::Tolerance T>
  [[nodiscard, gnu::pure]]
  std::expected<safety::NumericalTier<T, const NumericalRecipe*>, RecipeError>
  by_name_pinned(std::string_view name) const noexcept {
    auto base = by_name(name);
    if (!base) return std::unexpected(base.error());
    const NumericalRecipe* recipe = *base;
    // tolerance_of cannot be null (recipe pointer is guaranteed
    // non-null by by_name's contract).  Verify the runtime class
    // subsumes the static request: leq(Required, Actual).
    if (!safety::ToleranceLattice::leq(T, tolerance_of(*recipe))) {
      return std::unexpected(RecipeError::ToleranceMismatch);
    }
    return safety::NumericalTier<T, const NumericalRecipe*>{recipe};
  }

  // ── by_hash_pinned — hash lookup with tier admission ────────────
  template <safety::Tolerance T>
  [[nodiscard, gnu::pure]]
  std::expected<safety::NumericalTier<T, const NumericalRecipe*>, RecipeError>
  by_hash_pinned(RecipeHash hash) const noexcept {
    auto base = by_hash(hash);
    if (!base) return std::unexpected(base.error());
    const NumericalRecipe* recipe = *base;
    if (!safety::ToleranceLattice::leq(T, tolerance_of(*recipe))) {
      return std::unexpected(RecipeError::ToleranceMismatch);
    }
    return safety::NumericalTier<T, const NumericalRecipe*>{recipe};
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
                                      effects::Alloc a) noexcept
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

inline std::expected<const NumericalRecipe*, RecipeError>
RecipeRegistry::by_hash(RecipeHash hash) const noexcept
{
  // Linear scan — 8 × 8-byte compare = one cache line's worth of
  // work, faster than a hash-map probe would be.  The hash stored
  // on each interned recipe is authoritative (RecipePool guarantees
  // it via compute_recipe_hash) so comparing against recipe->hash
  // is the identity the caller expects.
  for (const auto& e : entries_) {
    if (e.recipe->hash == hash) {
      return e.recipe;
    }
  }
  return std::unexpected(RecipeError::HashNotFound);
}

}  // namespace crucible
