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
// Coverage axes (T01-T16):
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

  std::puts("ok");
  return 0;
}
