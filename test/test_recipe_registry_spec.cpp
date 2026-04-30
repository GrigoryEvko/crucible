// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// ═══════════════════════════════════════════════════════════════════
// test_recipe_registry_spec — FOUND-G78 production call site coverage.
//
// Exercises the RecipeSpec<const NumericalRecipe*> two-axis overlay
// on RecipeRegistry: `by_name_spec(name)` / `by_hash_spec(hash)` pair
// the canonical recipe pointer with its (tolerance_tier, recipe_family)
// at runtime via `tolerance_of(*r)` and `recipe_family_of(*r)`.
//
// Companion to FOUND-G04 (test_recipe_registry_tier).  Where G04 lifts
// a SINGLE-axis static admission (Tolerance) into the type via a
// regime-1 NumericalTier (zero-byte EBO-collapsed grade), G78 lifts
// TWO axes (Tolerance × RecipeFamily) into a runtime per-instance
// grade (regime-4, 2 bytes carried in the spec) — necessary because
// the two axes compose ALGEBRAICALLY DIFFERENTLY (chain × partial-
// order).  Combined coverage closes the recipe-admission surface:
//
//   FOUND-G04  → "tolerance class admits this consumer's request?"
//   FOUND-G78  → "tolerance AND family BOTH admit?  combinable
//                  pointwise on both axes when streams converge?"
//
// Coverage axes (T01-T23):
//   T01     recipe_family_of mapping correctness — every ReductionAlgo
//           variant maps to its RecipeFamily category
//   T02     by_name_spec hit path — populates BOTH axes from recipe
//   T03     by_name_spec for every starter recipe — spec round-trips
//           through tolerance_of + recipe_family_of
//   T04     by_name_spec NameNotFound propagation
//   T05     by_hash_spec hit path
//   T06     by_hash_spec HashNotFound propagation
//   T07     parity between by_name_spec and by_hash_spec on the same
//           recipe — same pointer, same spec
//   T08     admits() — the load-bearing Forge Phase E.RecipeSelect
//           dispatch gate.  Single-axis success, single-axis failure,
//           joint success
//   T09     admits() rejects on FAMILY mismatch even when tolerance
//           agrees — the bug class beyond what FOUND-G04 catches
//   T10     admits() rejects on TOLERANCE mismatch even when family
//           agrees
//   T11     admits() at sentinel positions — None bottom admits
//           nothing past itself; Any wildcard admits any family
//   T12     combine_max() pointwise join — same axis preserved
//   T13     combine_max() promotes sibling families to Any wildcard
//   T14     Layout invariant — sizeof(RecipeSpec<ptr>) >= sizeof(ptr)
//           + 2 bytes (regime-4 carries grade per instance)
//   T15     spec.peek() identity — same pointer as by_name returned
//   T16     determinism — repeated by_name_spec / by_hash_spec calls
//           on the same recipe produce specs that compare equal
//
// G78-AUDIT extended coverage (T17-T22) — matches the G04-AUDIT
// matrix-fill discipline lifted to the two-axis product lattice:
//   T17     Full Tolerance × RecipeFamily admission matrix sweep
//           (8 starter recipes × 7 tiers × 4 named families = 224
//           admission decisions, every one verified against
//           pointwise leq)
//   T18     Sibling-family adjacency probe — every C(4,2) = 6
//           unordered pair × 2 directions = 12 cross-family
//           rejections (M3-substructure witness at production)
//   T19     combine_max sibling-pair join sweep — every C(4,2) = 6
//           sibling pair joins to Any wildcard (regime-4 runtime
//           preservation of join semantics)
//   T20     Wildcard semantics — Any-family admits all four named
//           families; None-family admits ONLY None (lattice cap)
//   T21     Hash → Spec persistence-replay determinism — Cipher
//           recovery simulation: persist hash, drop registry,
//           re-construct, verify by_hash_spec admits the same
//           (tier, family) set as the live name-lookup
//   T22     Move-only T witness — RecipeSpec<MoveOnlyT> compiles
//           and preserves wrapper move-only contract through the
//           production overload surface (future-proofs for
//           OwnedRecipe / Linear<RecipeBlob> extensions)
//   T23     Sentinel-family admission semantics — orthogonal to
//           T20.  T20 exercised sentinel PRODUCERS (Any-spec /
//           None-spec); T23 exercises sentinel REQUESTS
//           (admits(*, None) / admits(*, Any)).  Concrete specs
//           admit None requests at every spec-admitted tier and
//           reject Any requests; wildcard specs admit both
//           sentinels.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <crucible/safety/RecipeSpec.h>

#include "test_assert.h"
#include <cassert>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

using crucible::Arena;
using crucible::NumericalRecipe;
using crucible::RecipeError;
using crucible::RecipeHash;
using crucible::RecipePool;
using crucible::RecipeRegistry;
using crucible::ReductionAlgo;
using crucible::ReductionDeterminism;
using crucible::ScalarType;
using crucible::recipe_family_of;
using crucible::tolerance_of;
using safety_Tolerance    = crucible::safety::Tolerance;
using safety_RecipeFamily = crucible::safety::RecipeFamily;
using crucible::safety::RecipeSpec;

namespace names = crucible::recipe_names;

crucible::effects::Test g_test{};
inline crucible::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }

}  // namespace

int main() {
  // ═══════════════════════════════════════════════════════════════
  // T01 — recipe_family_of maps every ReductionAlgo to its category.
  //       Compile-time witnesses for the four named algorithms; the
  //       mapping is closed (no wildcard / sentinel emerges from a
  //       concrete recipe — only from default-arm fallback).
  // ═══════════════════════════════════════════════════════════════
  {
    NumericalRecipe r_pairwise{};
    r_pairwise.reduction_algo = ReductionAlgo::PAIRWISE;
    assert(recipe_family_of(r_pairwise) == safety_RecipeFamily::Pairwise);

    NumericalRecipe r_linear{};
    r_linear.reduction_algo = ReductionAlgo::LINEAR;
    assert(recipe_family_of(r_linear) == safety_RecipeFamily::Linear);

    NumericalRecipe r_kahan{};
    r_kahan.reduction_algo = ReductionAlgo::KAHAN;
    assert(recipe_family_of(r_kahan) == safety_RecipeFamily::Kahan);

    NumericalRecipe r_block{};
    r_block.reduction_algo = ReductionAlgo::BLOCK_STABLE;
    assert(recipe_family_of(r_block) == safety_RecipeFamily::BlockStable);

    // gnu::const-tagged: no observable side effects, deterministic.
    static_assert(noexcept(recipe_family_of(std::declval<const NumericalRecipe&>())));
  }

  // Common pool + registry for the call-boundary tests below.
  Arena arena{};
  RecipePool pool{arena, alloc_cap()};
  RecipeRegistry reg{pool, alloc_cap()};

  // ═══════════════════════════════════════════════════════════════
  // T02 — by_name_spec hit path populates BOTH axes from the recipe.
  //       f32_strict (BITEXACT_STRICT + PAIRWISE) ⇒ spec at
  //       (BITEXACT, Pairwise).
  // ═══════════════════════════════════════════════════════════════
  {
    auto spec = reg.by_name_spec(names::kF32Strict);
    assert(spec.has_value());
    static_assert(std::is_same_v<
        decltype(spec)::value_type,
        RecipeSpec<const NumericalRecipe*>>);
    assert(spec->peek() != nullptr);
    assert(spec->peek()->determinism    == ReductionDeterminism::BITEXACT_STRICT);
    assert(spec->peek()->reduction_algo == ReductionAlgo::PAIRWISE);
    assert(spec->tolerance()            == safety_Tolerance::BITEXACT);
    assert(spec->recipe_family()        == safety_RecipeFamily::Pairwise);
  }

  // ═══════════════════════════════════════════════════════════════
  // T03 — Starter recipe sweep.  For every entry, the spec returned
  //       by by_name_spec must match the (tolerance_of, recipe_family_of)
  //       pair computed from the recipe directly.  This is the
  //       round-trip invariant that proves by_name_spec is just
  //       lookup-then-pair, no transformation.
  // ═══════════════════════════════════════════════════════════════
  {
    for (const auto& entry : reg.entries()) {
      auto spec = reg.by_name_spec(entry.name);
      assert(spec.has_value());
      assert(spec->peek()           == entry.recipe);
      assert(spec->tolerance()      == tolerance_of(*entry.recipe));
      assert(spec->recipe_family()  == recipe_family_of(*entry.recipe));
    }
  }

  // ═══════════════════════════════════════════════════════════════
  // T04 — by_name_spec propagates NameNotFound.  Pinned/spec is
  //       additive over by_name; an unknown name surfaces the
  //       primary-key error before ANY axis is examined.
  // ═══════════════════════════════════════════════════════════════
  {
    auto spec = reg.by_name_spec("nonexistent_recipe");
    assert(!spec.has_value());
    assert(spec.error() == RecipeError::NameNotFound);
  }

  // ═══════════════════════════════════════════════════════════════
  // T05 — by_hash_spec hit path.  Resolve f16_f32accum_tc by name
  //       (BITEXACT_TC + PAIRWISE, FP16 storage), then re-resolve
  //       by hash; spec must match.  Tolerance is ULP_FP16 (the
  //       1-ULP class for FP16 storage), family is Pairwise.
  // ═══════════════════════════════════════════════════════════════
  {
    auto base = reg.by_name(names::kF16F32AccumTc);
    assert(base.has_value());
    const RecipeHash hash = (*base)->hash;

    auto spec = reg.by_hash_spec(hash);
    assert(spec.has_value());
    assert(spec->peek()->hash    == hash);
    assert(spec->tolerance()     == safety_Tolerance::ULP_FP16);
    assert(spec->recipe_family() == safety_RecipeFamily::Pairwise);
  }

  // ═══════════════════════════════════════════════════════════════
  // T06 — by_hash_spec propagates HashNotFound.  Bogus hash yields
  //       HashNotFound (not any joint-axis variant — the spec
  //       overlay is purely additive on the lookup contract).
  // ═══════════════════════════════════════════════════════════════
  {
    const RecipeHash bogus{0xFEEDFACEDEADBEEFULL};
    auto spec = reg.by_hash_spec(bogus);
    assert(!spec.has_value());
    assert(spec.error() == RecipeError::HashNotFound);
  }

  // ═══════════════════════════════════════════════════════════════
  // T07 — Parity: by_name_spec(name) == by_hash_spec(recipe->hash)
  //       for every starter recipe.  Both paths must produce
  //       byte-equal specs; without this, Cipher recovery (hash
  //       lookup) would silently produce different admission
  //       behavior than the live registry (name lookup).
  // ═══════════════════════════════════════════════════════════════
  {
    for (const auto& entry : reg.entries()) {
      auto by_n = reg.by_name_spec(entry.name);
      auto by_h = reg.by_hash_spec(entry.recipe->hash);
      assert(by_n.has_value());
      assert(by_h.has_value());
      assert(by_n->peek()          == by_h->peek());
      assert(by_n->tolerance()     == by_h->tolerance());
      assert(by_n->recipe_family() == by_h->recipe_family());
      assert(*by_n == *by_h);
    }
  }

  // ═══════════════════════════════════════════════════════════════
  // T08 — admits() — the load-bearing Forge Phase E.RecipeSelect
  //       gate.  Three sub-cases:
  //         (a) joint success at exact match
  //         (b) tier weaker than spec OK (subsumes)
  //         (c) family more permissive (None bottom) OK
  //
  //       Producer = f16_f32accum_tc (ULP_FP16 + Pairwise).
  // ═══════════════════════════════════════════════════════════════
  {
    auto spec = reg.by_name_spec(names::kF16F32AccumTc).value();

    // (a) exact match: ULP_FP16 + Pairwise admits ULP_FP16 + Pairwise.
    assert(spec.admits(safety_Tolerance::ULP_FP16,
                       safety_RecipeFamily::Pairwise));

    // (b) tier weaker (lattice-down): ULP_FP8 ⊑ ULP_FP16, so a
    //     ULP_FP16 producer admits a ULP_FP8 consumer.
    assert(spec.admits(safety_Tolerance::ULP_FP8,
                       safety_RecipeFamily::Pairwise));

    // (c) family wildcard at the bottom: None ⊑ Pairwise, so the
    //     producer admits a "no specific recipe required" consumer.
    assert(spec.admits(safety_Tolerance::ULP_FP16,
                       safety_RecipeFamily::None));
  }

  // ═══════════════════════════════════════════════════════════════
  // T09 — admits() rejects on FAMILY mismatch even when tolerance
  //       agrees.  The bug class FOUND-G04 cannot catch.
  //       Producer = f32_strict (BITEXACT + Pairwise).  Consumer
  //       requests BITEXACT but with Kahan family — same tier, but
  //       Kahan is a SIBLING of Pairwise (incomparable in the
  //       partial-order).  Reject.
  // ═══════════════════════════════════════════════════════════════
  {
    auto spec = reg.by_name_spec(names::kF32Strict).value();
    assert(spec.tolerance()     == safety_Tolerance::BITEXACT);
    assert(spec.recipe_family() == safety_RecipeFamily::Pairwise);

    // Tolerance agrees, family disagrees — REJECT.
    assert(!spec.admits(safety_Tolerance::BITEXACT,
                        safety_RecipeFamily::Kahan));
    assert(!spec.admits(safety_Tolerance::BITEXACT,
                        safety_RecipeFamily::BlockStable));
    assert(!spec.admits(safety_Tolerance::BITEXACT,
                        safety_RecipeFamily::Linear));
  }

  // ═══════════════════════════════════════════════════════════════
  // T10 — admits() rejects on TOLERANCE mismatch even when family
  //       agrees.  Producer = f32_ordered (RELAXED + Pairwise).
  //       Consumer requests BITEXACT + Pairwise — same family, but
  //       RELAXED ≮ BITEXACT in the tolerance chain.  Reject.
  // ═══════════════════════════════════════════════════════════════
  {
    auto spec = reg.by_name_spec(names::kF32Ordered).value();
    assert(spec.tolerance()     == safety_Tolerance::RELAXED);
    assert(spec.recipe_family() == safety_RecipeFamily::Pairwise);

    // Family agrees, tolerance disagrees — REJECT.
    assert(!spec.admits(safety_Tolerance::BITEXACT,
                        safety_RecipeFamily::Pairwise));
    assert(!spec.admits(safety_Tolerance::ULP_FP16,
                        safety_RecipeFamily::Pairwise));
  }

  // ═══════════════════════════════════════════════════════════════
  // T11 — admits() at sentinel positions.
  //         - None (family bottom):  admitted only by family-None
  //                                   consumers (and Any consumers).
  //         - Any (family top):      admits any family request.
  // ═══════════════════════════════════════════════════════════════
  {
    // Synthesize a spec at (RELAXED, None) — the lattice bottom.
    RecipeSpec<const NumericalRecipe*> bottom_spec{
        nullptr, safety_Tolerance::RELAXED, safety_RecipeFamily::None};
    assert(bottom_spec.admits(safety_Tolerance::RELAXED,
                              safety_RecipeFamily::None));
    assert(!bottom_spec.admits(safety_Tolerance::RELAXED,
                               safety_RecipeFamily::Pairwise));
    assert(!bottom_spec.admits(safety_Tolerance::ULP_FP16,
                               safety_RecipeFamily::None));

    // Synthesize at (BITEXACT, Any) — the lattice top.
    RecipeSpec<const NumericalRecipe*> top_spec{
        nullptr, safety_Tolerance::BITEXACT, safety_RecipeFamily::Any};
    assert(top_spec.admits(safety_Tolerance::BITEXACT,
                           safety_RecipeFamily::Pairwise));
    assert(top_spec.admits(safety_Tolerance::BITEXACT,
                           safety_RecipeFamily::Kahan));
    assert(top_spec.admits(safety_Tolerance::BITEXACT,
                           safety_RecipeFamily::BlockStable));
    assert(top_spec.admits(safety_Tolerance::ULP_FP8,
                           safety_RecipeFamily::Linear));
  }

  // ═══════════════════════════════════════════════════════════════
  // T12 — combine_max() pointwise join.  Two specs at the same
  //       (tier, family) joined with themselves preserve both axes
  //       (idempotence on the product lattice).
  // ═══════════════════════════════════════════════════════════════
  {
    auto a = reg.by_name_spec(names::kF16F32AccumTc).value();   // (ULP_FP16, Pairwise)
    auto b = reg.by_name_spec(names::kF16F32AccumTc).value();   // (ULP_FP16, Pairwise)
    auto c = a.combine_max(b);
    assert(c.tolerance()     == safety_Tolerance::ULP_FP16);
    assert(c.recipe_family() == safety_RecipeFamily::Pairwise);
  }

  // ═══════════════════════════════════════════════════════════════
  // T13 — combine_max() promotes sibling families to Any.  Two
  //       starter recipes share the Pairwise family today, so we
  //       synthesize a Kahan-family spec to exercise the M3
  //       substructure (Pairwise ∨ Kahan = Any).  Tier joins to the
  //       MAX of the two (BITEXACT in this case).
  // ═══════════════════════════════════════════════════════════════
  {
    auto a = reg.by_name_spec(names::kF32Strict).value();      // (BITEXACT, Pairwise)
    RecipeSpec<const NumericalRecipe*> synth_kahan{
        a.peek(), safety_Tolerance::ULP_FP16, safety_RecipeFamily::Kahan};

    auto joined = a.combine_max(synth_kahan);
    // Tier: max(BITEXACT, ULP_FP16) = BITEXACT.
    assert(joined.tolerance() == safety_Tolerance::BITEXACT);
    // Family: siblings → Any wildcard.
    assert(joined.recipe_family() == safety_RecipeFamily::Any);
  }

  // ═══════════════════════════════════════════════════════════════
  // T14 — Layout invariant.  Regime-4 (per-instance grade carried)
  //       — sizeof must accommodate the pointer + 2 grade bytes
  //       (Tolerance + RecipeFamily, both uint8_t).  No EBO collapse
  //       possible because the grade IS runtime data.
  // ═══════════════════════════════════════════════════════════════
  {
    using SpecPtr = RecipeSpec<const NumericalRecipe*>;
    static_assert(sizeof(SpecPtr) >= sizeof(const NumericalRecipe*) + 2);
    // Type identity vs the equivalent for a different T (sanity).
    static_assert(!std::is_same_v<SpecPtr, RecipeSpec<int>>);
  }

  // ═══════════════════════════════════════════════════════════════
  // T15 — spec.peek() identity.  The pointer carried by the spec
  //       MUST be the same canonical interned pointer that by_name
  //       returns directly — RecipeSpec is a passive overlay, not
  //       a copy / clone.
  // ═══════════════════════════════════════════════════════════════
  {
    auto base = reg.by_name(names::kF32Strict);
    assert(base.has_value());
    const NumericalRecipe* expected_ptr = *base;

    auto spec = reg.by_name_spec(names::kF32Strict).value();
    assert(spec.peek() == expected_ptr);

    const NumericalRecipe* moved = std::move(spec).consume();
    assert(moved == expected_ptr);
  }

  // ═══════════════════════════════════════════════════════════════
  // T16 — Determinism.  Repeated by_name_spec / by_hash_spec calls
  //       on the SAME recipe must produce specs that compare equal
  //       (operator== inherited from RecipeSpec, compares peek +
  //       tolerance + recipe_family).  Captures the load-bearing
  //       no-hidden-state invariant for replay.
  // ═══════════════════════════════════════════════════════════════
  {
    auto s1 = reg.by_name_spec(names::kBf16F32AccumTc).value();
    auto s2 = reg.by_name_spec(names::kBf16F32AccumTc).value();
    auto s3 = reg.by_hash_spec(s1.peek()->hash).value();
    assert(s1 == s2);
    assert(s1 == s3);

    // Different recipes must NOT compare equal — sanity that the
    // operator is not a no-op.
    auto s4 = reg.by_name_spec(names::kF32Strict).value();
    assert(!(s1 == s4));
  }

  // ═══════════════════════════════════════════════════════════════
  // FOUND-G78-AUDIT — extended coverage matching the G04-AUDIT
  //                   matrix-fill discipline for the two-axis
  //                   product lattice.
  // ═══════════════════════════════════════════════════════════════

  // ═══════════════════════════════════════════════════════════════
  // T17 — Full Tolerance × RecipeFamily admission matrix sweep.
  //       For every starter recipe, probe the FULL grid of
  //       (req_tier, req_family) admission requests across all 7
  //       Tolerance tiers and all 4 named RecipeFamily values
  //       (excluding sentinels — they're covered in T11/T20).
  //       Each cell's admit/reject decision MUST agree with the
  //       pointwise leq predicate over each axis.
  //
  //       Coverage: 8 starter recipes × 7 tiers × 4 named families
  //       = 224 admission decisions, every one verified against
  //       both the pre-computed expectation AND the runtime
  //       admits() return.  Catches any drift between the runtime
  //       admit() body and the underlying lattice leq predicates.
  // ═══════════════════════════════════════════════════════════════
  {
    using crucible::algebra::lattices::ToleranceLattice;
    using crucible::algebra::lattices::RecipeFamilyLattice;

    constexpr safety_Tolerance kTiers[] = {
        safety_Tolerance::RELAXED,
        safety_Tolerance::ULP_INT8,
        safety_Tolerance::ULP_FP8,
        safety_Tolerance::ULP_FP16,
        safety_Tolerance::ULP_FP32,
        safety_Tolerance::ULP_FP64,
        safety_Tolerance::BITEXACT,
    };
    constexpr safety_RecipeFamily kFamilies[] = {
        safety_RecipeFamily::Linear,
        safety_RecipeFamily::Pairwise,
        safety_RecipeFamily::Kahan,
        safety_RecipeFamily::BlockStable,
    };

    int total_decisions = 0;
    for (const auto& entry : reg.entries()) {
      auto spec = reg.by_name_spec(entry.name).value();
      const auto spec_tier = spec.tolerance();
      const auto spec_fam  = spec.recipe_family();
      for (auto req_tier : kTiers) {
        for (auto req_fam : kFamilies) {
          const bool tier_ok =
              ToleranceLattice::leq(req_tier, spec_tier);
          const bool fam_ok  =
              RecipeFamilyLattice::leq(req_fam, spec_fam);
          const bool expected = tier_ok && fam_ok;
          assert(spec.admits(req_tier, req_fam) == expected);
          ++total_decisions;
        }
      }
    }
    assert(total_decisions == 8 * 7 * 4);  // 224
  }

  // ═══════════════════════════════════════════════════════════════
  // T18 — Sibling-family adjacency probe.  For every UNORDERED
  //       PAIR of distinct named families {Linear, Pairwise,
  //       Kahan, BlockStable}, construct two synthetic specs at
  //       the SAME tolerance tier but at SIBLING families.  Each
  //       must reject the OTHER's family request (M3-substructure
  //       witness lifted from the production wrapper into the
  //       call-site context).  C(4,2) = 6 unordered pairs × 2
  //       directions = 12 cross-family rejections.
  // ═══════════════════════════════════════════════════════════════
  {
    constexpr safety_RecipeFamily kFamilies[] = {
        safety_RecipeFamily::Linear,
        safety_RecipeFamily::Pairwise,
        safety_RecipeFamily::Kahan,
        safety_RecipeFamily::BlockStable,
    };

    int rejection_count = 0;
    for (size_t i = 0; i < 4; ++i) {
      for (size_t j = i + 1; j < 4; ++j) {
        // Synthesize specs at the same tier but with sibling
        // families.  Both directions must reject.
        RecipeSpec<const NumericalRecipe*> spec_i{
            nullptr, safety_Tolerance::BITEXACT, kFamilies[i]};
        RecipeSpec<const NumericalRecipe*> spec_j{
            nullptr, safety_Tolerance::BITEXACT, kFamilies[j]};
        // i admits j? — should be no.
        assert(!spec_i.admits(safety_Tolerance::BITEXACT, kFamilies[j]));
        // j admits i? — should be no.
        assert(!spec_j.admits(safety_Tolerance::BITEXACT, kFamilies[i]));
        rejection_count += 2;
      }
    }
    assert(rejection_count == 12);
  }

  // ═══════════════════════════════════════════════════════════════
  // T19 — combine_max joins to Any across every sibling pair.  The
  //       M3-substructure witness from the wrapper repeated at the
  //       production-call-site level.  Six unordered sibling pairs
  //       all promote to Any.  Verifies the join semantics survive
  //       the regime-4 runtime carriage (no degradation between
  //       wrapper-level and production-level join).
  // ═══════════════════════════════════════════════════════════════
  {
    constexpr safety_RecipeFamily kFamilies[] = {
        safety_RecipeFamily::Linear,
        safety_RecipeFamily::Pairwise,
        safety_RecipeFamily::Kahan,
        safety_RecipeFamily::BlockStable,
    };

    auto base_recipe = reg.by_name_spec(names::kF32Strict).value().peek();

    int join_count = 0;
    for (size_t i = 0; i < 4; ++i) {
      for (size_t j = i + 1; j < 4; ++j) {
        RecipeSpec<const NumericalRecipe*> spec_i{
            base_recipe, safety_Tolerance::ULP_FP16, kFamilies[i]};
        RecipeSpec<const NumericalRecipe*> spec_j{
            base_recipe, safety_Tolerance::ULP_FP16, kFamilies[j]};
        auto joined = spec_i.combine_max(spec_j);
        assert(joined.tolerance()     == safety_Tolerance::ULP_FP16);
        assert(joined.recipe_family() == safety_RecipeFamily::Any);
        ++join_count;
      }
    }
    assert(join_count == 6);  // C(4,2)
  }

  // ═══════════════════════════════════════════════════════════════
  // T20 — Wildcard semantics — Any-family spec admits any of the
  //       four named families at every tier; None-family spec
  //       admits ONLY a None request.  Caps the partial-order axis.
  // ═══════════════════════════════════════════════════════════════
  {
    constexpr safety_RecipeFamily kFamilies[] = {
        safety_RecipeFamily::Linear,
        safety_RecipeFamily::Pairwise,
        safety_RecipeFamily::Kahan,
        safety_RecipeFamily::BlockStable,
    };

    // Top wildcard at BITEXACT — admits every named family.
    RecipeSpec<const NumericalRecipe*> any_spec{
        nullptr, safety_Tolerance::BITEXACT, safety_RecipeFamily::Any};
    for (auto fam : kFamilies) {
      assert(any_spec.admits(safety_Tolerance::BITEXACT, fam));
    }
    // None-bottom is also admissible at the wildcard.
    assert(any_spec.admits(safety_Tolerance::BITEXACT,
                           safety_RecipeFamily::None));

    // Bottom None at BITEXACT — admits ONLY None.
    RecipeSpec<const NumericalRecipe*> none_spec{
        nullptr, safety_Tolerance::BITEXACT, safety_RecipeFamily::None};
    for (auto fam : kFamilies) {
      assert(!none_spec.admits(safety_Tolerance::BITEXACT, fam));
    }
    assert(none_spec.admits(safety_Tolerance::BITEXACT,
                            safety_RecipeFamily::None));
  }

  // ═══════════════════════════════════════════════════════════════
  // T21 — Hash → Spec persistence-replay determinism.  For every
  //       starter recipe simulate a Cipher load: persist hash, drop
  //       the registry, re-construct, look up by hash, verify the
  //       resolved spec admits the SAME (req_tier, req_family) set
  //       as the live name-lookup did.  Captures the load-bearing
  //       Cipher recovery → recipe-admission consistency invariant.
  // ═══════════════════════════════════════════════════════════════
  {
    using crucible::algebra::lattices::ToleranceLattice;
    using crucible::algebra::lattices::RecipeFamilyLattice;

    constexpr safety_Tolerance kTiers[] = {
        safety_Tolerance::RELAXED,
        safety_Tolerance::ULP_FP16,
        safety_Tolerance::BITEXACT,
    };
    constexpr safety_RecipeFamily kFamilies[] = {
        safety_RecipeFamily::None,
        safety_RecipeFamily::Pairwise,
        safety_RecipeFamily::Kahan,
        safety_RecipeFamily::Any,
    };

    // Snapshot every starter's hash.
    std::array<RecipeHash, RecipeRegistry::STARTER_COUNT> persisted{};
    for (std::size_t i = 0; i < RecipeRegistry::STARTER_COUNT; ++i) {
      persisted[i] = reg.entries()[i].recipe->hash;
    }

    // Re-construct on a fresh pool / arena — simulates Cipher
    // loading the persisted hash on a fresh process.
    Arena    arena2{};
    RecipePool     pool2{arena2, alloc_cap()};
    RecipeRegistry reg2{pool2, alloc_cap()};

    for (std::size_t i = 0; i < RecipeRegistry::STARTER_COUNT; ++i) {
      auto live_spec = reg.by_name_spec(reg.entries()[i].name).value();
      auto recovered = reg2.by_hash_spec(persisted[i]).value();

      // Same axes — admission must agree on every probe.
      assert(live_spec.tolerance()     == recovered.tolerance());
      assert(live_spec.recipe_family() == recovered.recipe_family());

      for (auto t : kTiers) {
        for (auto f : kFamilies) {
          assert(live_spec.admits(t, f) == recovered.admits(t, f));
        }
      }
    }
  }

  // ═══════════════════════════════════════════════════════════════
  // T22 — Move-only T witness.  RecipeSpec<MoveOnlyT> compiles and
  //       carries the wrapper's move-only contract through the
  //       production overload surface.  The registry today returns
  //       const NumericalRecipe* (trivially copyable) but the
  //       admission contract via .admits() / combine_max() must
  //       work for arbitrary T including non-copyable carriers —
  //       captures the future-proofing for OwnedRecipe<T> /
  //       Linear<RecipeBlob> production extensions.
  // ═══════════════════════════════════════════════════════════════
  {
    struct MoveOnlyT {
      int v{0};
      MoveOnlyT() = default;
      explicit MoveOnlyT(int x) : v{x} {}
      MoveOnlyT(MoveOnlyT&&) = default;
      MoveOnlyT& operator=(MoveOnlyT&&) = default;
      MoveOnlyT(MoveOnlyT const&) = delete;
      MoveOnlyT& operator=(MoveOnlyT const&) = delete;
    };

    static_assert(!std::is_copy_constructible_v<RecipeSpec<MoveOnlyT>>);
    static_assert( std::is_move_constructible_v<RecipeSpec<MoveOnlyT>>);

    RecipeSpec<MoveOnlyT> a{
        MoveOnlyT{42}, safety_Tolerance::ULP_FP16,
        safety_RecipeFamily::Kahan};
    RecipeSpec<MoveOnlyT> b{
        MoveOnlyT{99}, safety_Tolerance::BITEXACT,
        safety_RecipeFamily::Kahan};

    // combine_max && rvalue overload must work.
    auto joined = std::move(a).combine_max(b);
    assert(joined.tolerance()     == safety_Tolerance::BITEXACT);
    assert(joined.recipe_family() == safety_RecipeFamily::Kahan);
    assert(joined.peek().v        == 42);

    // admits() works on move-only carriers.
    assert(joined.admits(safety_Tolerance::ULP_FP8,
                         safety_RecipeFamily::Kahan));
    assert(!joined.admits(safety_Tolerance::ULP_FP16,
                          safety_RecipeFamily::Pairwise));
  }

  // ═══════════════════════════════════════════════════════════════
  // T23 — Sentinel-family admission semantics — orthogonal axis
  //       to T20 which exercised sentinel PRODUCERS (Any-spec /
  //       None-spec).  T23 exercises sentinel CONSUMERS — the
  //       admits() request side at the partial-order axis caps:
  //
  //         req_family = None   — bottom: leq(None, anything) = true.
  //                                Every concrete-family spec admits
  //                                this request.  Production: a
  //                                consumer that doesn't pin a
  //                                family (recipe-agnostic data path)
  //                                always admits.
  //         req_family = Any    — top: leq(Any, X) = true ONLY when
  //                                X = Any.  Concrete-family specs
  //                                REJECT an Any-family request.
  //                                Production: a consumer demanding
  //                                "any family acceptable" passes
  //                                only at wildcard producers.
  //
  //       Without this nuance, the sentinel cap could silently
  //       admit / reject in ways that contradict the lattice.
  //       Catches that drift at the call-site level.
  // ═══════════════════════════════════════════════════════════════
  {
    // Concrete-family producer: f32_strict (BITEXACT, Pairwise).
    auto strict_spec = reg.by_name_spec(names::kF32Strict).value();
    assert(strict_spec.recipe_family() == safety_RecipeFamily::Pairwise);

    // Bottom request — admitted at the strict spec's tier.
    assert(strict_spec.admits(safety_Tolerance::BITEXACT,
                              safety_RecipeFamily::None));
    // Top request — REJECTED (concrete spec, not wildcard).
    assert(!strict_spec.admits(safety_Tolerance::BITEXACT,
                               safety_RecipeFamily::Any));

    // Sweep all 8 starter recipes — every concrete-family spec
    // admits None and rejects Any (at any tier the spec admits).
    int none_admits = 0;
    int any_rejects = 0;
    for (const auto& entry : reg.entries()) {
      auto spec = reg.by_name_spec(entry.name).value();
      // The spec's own tier is the tightest tier admitted, so use
      // it for both the None-admit and Any-reject probes.
      if (spec.admits(spec.tolerance(), safety_RecipeFamily::None))
        ++none_admits;
      if (!spec.admits(spec.tolerance(), safety_RecipeFamily::Any))
        ++any_rejects;
    }
    assert(none_admits == int{RecipeRegistry::STARTER_COUNT});
    assert(any_rejects == int{RecipeRegistry::STARTER_COUNT});

    // Wildcard producer at BITEXACT — admits BOTH sentinels.
    RecipeSpec<const NumericalRecipe*> wildcard_spec{
        nullptr, safety_Tolerance::BITEXACT, safety_RecipeFamily::Any};
    assert(wildcard_spec.admits(safety_Tolerance::BITEXACT,
                                safety_RecipeFamily::None));
    assert(wildcard_spec.admits(safety_Tolerance::BITEXACT,
                                safety_RecipeFamily::Any));

    // Bottom None-family producer at BITEXACT — admits only None
    // request, rejects Any (since None ≮ Any in this direction —
    // wait, None IS below Any, so leq(Any, None) = false:
    // a None-family spec REJECTS an Any-family request).
    RecipeSpec<const NumericalRecipe*> none_spec{
        nullptr, safety_Tolerance::BITEXACT, safety_RecipeFamily::None};
    assert(none_spec.admits(safety_Tolerance::BITEXACT,
                            safety_RecipeFamily::None));
    assert(!none_spec.admits(safety_Tolerance::BITEXACT,
                             safety_RecipeFamily::Any));
  }

  std::puts("ok");
  return 0;
}
