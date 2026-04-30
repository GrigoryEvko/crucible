// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// ═══════════════════════════════════════════════════════════════════
// test_recipe_registry_tier — FOUND-G04 production call site coverage.
//
// Exercises the NumericalTier-pinned overlay on RecipeRegistry:
// `by_name_pinned<Tolerance T>(name)` and `by_hash_pinned<Tolerance T>
// (hash)` lift the recipe's runtime tolerance class into the type
// system, returning either a NumericalTier-wrapped pointer (admit) or
// RecipeError::ToleranceMismatch (reject).
//
// Coverage axes (T01-T15):
//   T01-T02 tolerance_for_dtype + tolerance_of mapping correctness
//   T03     by_name_pinned succeeds at exact tier (BITEXACT_STRICT
//           recipe → BITEXACT pin)
//   T04     by_name_pinned succeeds at strictly-weaker tier (BITEXACT
//           recipe → RELAXED pin via subsumption)
//   T05     by_name_pinned rejects when recipe's tolerance does not
//           subsume the static tier (ORDERED → BITEXACT pin)
//   T06     by_name_pinned propagates NameNotFound (priority over
//           tolerance check)
//   T07     by_hash_pinned hit at exact tier
//   T08     by_hash_pinned ToleranceMismatch
//   T09     by_hash_pinned propagates HashNotFound
//   T10     starter recipe sweep — full subsumption matrix
//   T11     RELAXED admits every starter recipe (lattice bottom)
//   T12     BITEXACT admits exactly BITEXACT_STRICT recipes
//   T13     layout invariant — sizeof(NumericalTier<T, const Recipe*>)
//           == sizeof(const Recipe*)
//   T14     pinned wrapper preserves recipe identity (consume + peek
//           round-trip)
//   T15     pinned wrappers at distinct tiers are type-distinct
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/NumericalTier.h>

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
using crucible::ReductionDeterminism;
using crucible::ScalarType;
using crucible::tolerance_for_dtype;
using crucible::tolerance_of;
using safety_Tolerance = crucible::safety::Tolerance;
using crucible::safety::NumericalTier;

namespace names = crucible::recipe_names;

crucible::effects::Test g_test{};
inline crucible::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }

}  // namespace

int main() {
  // ═══════════════════════════════════════════════════════════════
  // T01 — tolerance_for_dtype maps every storage dtype to its 1-ULP
  //       class.  Conservative discipline: integer dtypes → ULP_INT8;
  //       FP variants → matching FP class; unknown → RELAXED.
  // ═══════════════════════════════════════════════════════════════
  {
    static_assert(tolerance_for_dtype(ScalarType::Double)
                  == safety_Tolerance::ULP_FP64);
    static_assert(tolerance_for_dtype(ScalarType::Float)
                  == safety_Tolerance::ULP_FP32);
    static_assert(tolerance_for_dtype(ScalarType::Half)
                  == safety_Tolerance::ULP_FP16);
    static_assert(tolerance_for_dtype(ScalarType::BFloat16)
                  == safety_Tolerance::ULP_FP16);
    static_assert(tolerance_for_dtype(ScalarType::Float8_e4m3fn)
                  == safety_Tolerance::ULP_FP8);
    static_assert(tolerance_for_dtype(ScalarType::Float8_e5m2)
                  == safety_Tolerance::ULP_FP8);
    static_assert(tolerance_for_dtype(ScalarType::Char)
                  == safety_Tolerance::ULP_INT8);
    static_assert(tolerance_for_dtype(ScalarType::Byte)
                  == safety_Tolerance::ULP_INT8);
    // Unknown / non-FP / non-int dtypes default to RELAXED.
    static_assert(tolerance_for_dtype(ScalarType::Int)
                  == safety_Tolerance::RELAXED);
    static_assert(tolerance_for_dtype(ScalarType::Long)
                  == safety_Tolerance::RELAXED);
    static_assert(tolerance_for_dtype(ScalarType::Bool)
                  == safety_Tolerance::RELAXED);
    static_assert(tolerance_for_dtype(ScalarType::Undefined)
                  == safety_Tolerance::RELAXED);
  }

  // ═══════════════════════════════════════════════════════════════
  // T02 — tolerance_of maps (determinism, out_dtype) → admitted
  //       tolerance class.  BITEXACT_STRICT is the ONLY recipe class
  //       that admits BITEXACT.  ORDERED is conservatively RELAXED
  //       (≤4 ULP coarser than any 1-ULP-at-precision class).
  // ═══════════════════════════════════════════════════════════════
  {
    NumericalRecipe r_strict{};
    r_strict.determinism = ReductionDeterminism::BITEXACT_STRICT;
    r_strict.out_dtype   = ScalarType::Float;
    assert(tolerance_of(r_strict) == safety_Tolerance::BITEXACT);

    NumericalRecipe r_tc_fp16{};
    r_tc_fp16.determinism = ReductionDeterminism::BITEXACT_TC;
    r_tc_fp16.out_dtype   = ScalarType::Half;
    assert(tolerance_of(r_tc_fp16) == safety_Tolerance::ULP_FP16);

    NumericalRecipe r_tc_bf16{};
    r_tc_bf16.determinism = ReductionDeterminism::BITEXACT_TC;
    r_tc_bf16.out_dtype   = ScalarType::BFloat16;
    assert(tolerance_of(r_tc_bf16) == safety_Tolerance::ULP_FP16);

    NumericalRecipe r_ordered{};
    r_ordered.determinism = ReductionDeterminism::ORDERED;
    r_ordered.out_dtype   = ScalarType::Float;
    assert(tolerance_of(r_ordered) == safety_Tolerance::RELAXED);

    NumericalRecipe r_unordered{};
    r_unordered.determinism = ReductionDeterminism::UNORDERED;
    r_unordered.out_dtype   = ScalarType::Float;
    assert(tolerance_of(r_unordered) == safety_Tolerance::RELAXED);
  }

  // Common pool + registry for the call-boundary tests below.
  Arena arena{};
  RecipePool pool{arena, alloc_cap()};
  RecipeRegistry reg{pool, alloc_cap()};

  // ═══════════════════════════════════════════════════════════════
  // T03 — by_name_pinned succeeds at the recipe's exact tier.
  //       f32_strict (BITEXACT_STRICT) ⇒ tolerance_of == BITEXACT;
  //       requesting <BITEXACT> admits the recipe.
  // ═══════════════════════════════════════════════════════════════
  {
    auto pinned =
        reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Strict);
    assert(pinned.has_value());
    static_assert(std::is_same_v<
        decltype(pinned)::value_type,
        NumericalTier<safety_Tolerance::BITEXACT, const NumericalRecipe*>>);
    assert(pinned->peek() != nullptr);
    assert(pinned->peek()->determinism
                          == ReductionDeterminism::BITEXACT_STRICT);
  }

  // ═══════════════════════════════════════════════════════════════
  // T04 — by_name_pinned succeeds at strictly-weaker static tier.
  //       BITEXACT-class recipe trivially satisfies a RELAXED-pinned
  //       consumer (subsumption-up: stronger admits to weaker).
  // ═══════════════════════════════════════════════════════════════
  {
    auto pinned =
        reg.by_name_pinned<safety_Tolerance::RELAXED>(names::kF32Strict);
    assert(pinned.has_value());
    static_assert(std::is_same_v<
        decltype(pinned)::value_type,
        NumericalTier<safety_Tolerance::RELAXED, const NumericalRecipe*>>);
  }

  // ═══════════════════════════════════════════════════════════════
  // T05 — by_name_pinned ToleranceMismatch on UP-the-lattice request.
  //       f32_ordered (ORDERED) maps to RELAXED; requesting BITEXACT
  //       fails — RELAXED does not satisfy BITEXACT.  Captures the
  //       canonical "ORDERED 4-ULP recipe silently flowed into
  //       BITEXACT consumer" bug class.
  // ═══════════════════════════════════════════════════════════════
  {
    auto pinned =
        reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Ordered);
    assert(!pinned.has_value());
    assert(pinned.error() == RecipeError::ToleranceMismatch);
  }

  // ═══════════════════════════════════════════════════════════════
  // T06 — NameNotFound takes priority over tolerance check.  An
  //       unknown name surfaces NameNotFound regardless of the
  //       static tier's permissiveness — pinned is additive, never
  //       changes the non-pinned contract.
  // ═══════════════════════════════════════════════════════════════
  {
    auto pinned =
        reg.by_name_pinned<safety_Tolerance::RELAXED>("nonexistent_recipe");
    assert(!pinned.has_value());
    assert(pinned.error() == RecipeError::NameNotFound);

    auto pinned_strict =
        reg.by_name_pinned<safety_Tolerance::BITEXACT>("does_not_exist");
    assert(!pinned_strict.has_value());
    // Even with the strictest pin, NameNotFound wins — name is the
    // primary key, tolerance is the secondary admission gate.
    assert(pinned_strict.error() == RecipeError::NameNotFound);
  }

  // ═══════════════════════════════════════════════════════════════
  // T07 — by_hash_pinned hit at exact tier.  Resolve f32_strict by
  //       name, then re-resolve by hash with a BITEXACT pin.
  // ═══════════════════════════════════════════════════════════════
  {
    auto base = reg.by_name(names::kF32Strict);
    assert(base.has_value());
    const RecipeHash hash = (*base)->hash;

    auto pinned = reg.by_hash_pinned<safety_Tolerance::BITEXACT>(hash);
    assert(pinned.has_value());
    assert(pinned->peek()->hash == hash);
  }

  // ═══════════════════════════════════════════════════════════════
  // T08 — by_hash_pinned ToleranceMismatch.  Resolve an ORDERED
  //       recipe, then attempt to pin it as BITEXACT_TC's class
  //       (ULP_FP16 for FP16-storage); fails — ORDERED maps to
  //       RELAXED, not ULP_FP16.
  // ═══════════════════════════════════════════════════════════════
  {
    auto base = reg.by_name(names::kF16F32AccumOrdered);
    assert(base.has_value());
    const RecipeHash hash = (*base)->hash;

    auto pinned = reg.by_hash_pinned<safety_Tolerance::ULP_FP16>(hash);
    assert(!pinned.has_value());
    assert(pinned.error() == RecipeError::ToleranceMismatch);
  }

  // ═══════════════════════════════════════════════════════════════
  // T09 — by_hash_pinned propagates HashNotFound.  Synthesize a
  //       hash that no starter has; expect HashNotFound (not
  //       ToleranceMismatch — registry doesn't see the recipe at
  //       all).
  // ═══════════════════════════════════════════════════════════════
  {
    const RecipeHash bogus{0xDEADBEEFCAFEBABEULL};
    auto pinned = reg.by_hash_pinned<safety_Tolerance::RELAXED>(bogus);
    assert(!pinned.has_value());
    assert(pinned.error() == RecipeError::HashNotFound);
  }

  // ═══════════════════════════════════════════════════════════════
  // T10 — Starter recipe subsumption sweep.  For each starter recipe,
  //       verify that:
  //         - by_name_pinned<RELAXED> always succeeds (lattice bot).
  //         - by_name_pinned<BITEXACT> succeeds iff the recipe's
  //           determinism is BITEXACT_STRICT.
  // ═══════════════════════════════════════════════════════════════
  {
    for (const auto& entry : reg.entries()) {
      // RELAXED is the lattice bottom — every recipe satisfies it.
      auto relaxed_pin =
          reg.by_name_pinned<safety_Tolerance::RELAXED>(entry.name);
      assert(relaxed_pin.has_value());

      // BITEXACT is the lattice top — only BITEXACT_STRICT recipes
      // satisfy it.
      auto bitexact_pin =
          reg.by_name_pinned<safety_Tolerance::BITEXACT>(entry.name);
      const bool is_strict = entry.recipe->determinism
                             == ReductionDeterminism::BITEXACT_STRICT;
      assert(bitexact_pin.has_value() == is_strict);
    }
  }

  // ═══════════════════════════════════════════════════════════════
  // T11 — RELAXED admits every starter recipe (lattice bottom is
  //       trivially satisfied by every wrapper).
  // ═══════════════════════════════════════════════════════════════
  {
    int admits = 0;
    for (const auto& entry : reg.entries()) {
      auto pinned =
          reg.by_name_pinned<safety_Tolerance::RELAXED>(entry.name);
      if (pinned.has_value()) ++admits;
    }
    assert(admits == int{RecipeRegistry::STARTER_COUNT});
  }

  // ═══════════════════════════════════════════════════════════════
  // T12 — BITEXACT admits exactly BITEXACT_STRICT recipes.  In the
  //       starter set there is exactly ONE such recipe (f32_strict).
  // ═══════════════════════════════════════════════════════════════
  {
    int strict_admits = 0;
    for (const auto& entry : reg.entries()) {
      auto pinned =
          reg.by_name_pinned<safety_Tolerance::BITEXACT>(entry.name);
      if (pinned.has_value()) ++strict_admits;
    }
    assert(strict_admits == 1);
  }

  // ═══════════════════════════════════════════════════════════════
  // T13 — Layout invariant: NumericalTier-pinned wrapper is byte-
  //       equivalent to the bare const NumericalRecipe* under -O3.
  //       Empty At<>::element_type EBO-collapses inside Graded.
  // ═══════════════════════════════════════════════════════════════
  {
    using PinnedBitexact =
        NumericalTier<safety_Tolerance::BITEXACT, const NumericalRecipe*>;
    using PinnedRelaxed =
        NumericalTier<safety_Tolerance::RELAXED, const NumericalRecipe*>;
    using PinnedFp16 =
        NumericalTier<safety_Tolerance::ULP_FP16, const NumericalRecipe*>;
    static_assert(sizeof(PinnedBitexact) == sizeof(const NumericalRecipe*));
    static_assert(sizeof(PinnedRelaxed)  == sizeof(const NumericalRecipe*));
    static_assert(sizeof(PinnedFp16)     == sizeof(const NumericalRecipe*));
  }

  // ═══════════════════════════════════════════════════════════════
  // T14 — Pinned wrapper preserves recipe identity.  peek() returns
  //       the same pointer we'd get from by_name; consume() && moves
  //       the same pointer out.
  // ═══════════════════════════════════════════════════════════════
  {
    auto base = reg.by_name(names::kF32Strict);
    assert(base.has_value());
    const NumericalRecipe* expected_ptr = *base;

    auto pinned =
        reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Strict);
    assert(pinned.has_value());
    assert(pinned->peek() == expected_ptr);

    const NumericalRecipe* moved = std::move(*pinned).consume();
    assert(moved == expected_ptr);
  }

  // ═══════════════════════════════════════════════════════════════
  // T15 — Pinned wrappers at distinct tiers are type-distinct (no
  //       silent admission across tiers).  A function requiring
  //       satisfies<BITEXACT> rejects a NumericalTier<RELAXED, ...>
  //       at compile time — the load-bearing positive-inverse of
  //       T05.
  // ═══════════════════════════════════════════════════════════════
  {
    using PinnedBitexact =
        NumericalTier<safety_Tolerance::BITEXACT, const NumericalRecipe*>;
    using PinnedRelaxed =
        NumericalTier<safety_Tolerance::RELAXED, const NumericalRecipe*>;
    static_assert(!std::is_same_v<PinnedBitexact, PinnedRelaxed>);

    // BITEXACT subsumes RELAXED (chain top satisfies bottom).
    static_assert( PinnedBitexact::satisfies<safety_Tolerance::RELAXED>);
    // RELAXED does NOT subsume BITEXACT — load-bearing rejection.
    static_assert(!PinnedRelaxed::satisfies<safety_Tolerance::BITEXACT>);
  }

  // ═══════════════════════════════════════════════════════════════
  // FOUND-G04-AUDIT extended positive coverage (T16-T21)
  // ═══════════════════════════════════════════════════════════════

  // ── T16 — Full 7-tier relax DOWN chain ────────────────────────
  //
  // Lattice: RELAXED ⊑ ULP_INT8 ⊑ ULP_FP8 ⊑ ULP_FP16 ⊑ ULP_FP32 ⊑
  //          ULP_FP64 ⊑ BITEXACT.  Each step is DOWN-the-lattice
  //          (toward a less-strict tolerance class).  A BITEXACT-
  //          pinned value can be relaxed step-by-step through every
  //          tier and remain admissible at each consumer fence.
  // ═══════════════════════════════════════════════════════════════
  {
    using crucible::safety::NumericalTier;

    NumericalTier<safety_Tolerance::BITEXACT, int> bitexact{42};
    auto fp64    = std::move(bitexact).relax<safety_Tolerance::ULP_FP64>();
    auto fp32    = std::move(fp64).relax<safety_Tolerance::ULP_FP32>();
    auto fp16    = std::move(fp32).relax<safety_Tolerance::ULP_FP16>();
    auto fp8     = std::move(fp16).relax<safety_Tolerance::ULP_FP8>();
    auto int8    = std::move(fp8).relax<safety_Tolerance::ULP_INT8>();
    auto relaxed = std::move(int8).relax<safety_Tolerance::RELAXED>();

    static_assert(std::is_same_v<decltype(fp64),
        NumericalTier<safety_Tolerance::ULP_FP64, int>>);
    static_assert(std::is_same_v<decltype(fp32),
        NumericalTier<safety_Tolerance::ULP_FP32, int>>);
    static_assert(std::is_same_v<decltype(fp16),
        NumericalTier<safety_Tolerance::ULP_FP16, int>>);
    static_assert(std::is_same_v<decltype(fp8),
        NumericalTier<safety_Tolerance::ULP_FP8, int>>);
    static_assert(std::is_same_v<decltype(int8),
        NumericalTier<safety_Tolerance::ULP_INT8, int>>);
    static_assert(std::is_same_v<decltype(relaxed),
        NumericalTier<safety_Tolerance::RELAXED, int>>);

    int v = std::move(relaxed).consume();
    assert(v == 42);
  }

  // ── T17 — Reflective trait agreement (FOUND-D21) ──────────────
  //
  // is_numerical_tier_v + numerical_tier_v traits must agree with
  // the wrapper's static `tier` member across cv-ref qualifiers.
  // Drift would silently corrupt diagnostic printing and dispatcher
  // reading-surface introspection.
  // ═══════════════════════════════════════════════════════════════
  {
    using crucible::safety::extract::is_numerical_tier_v;
    using crucible::safety::extract::numerical_tier_v;

    using NT_BX = NumericalTier<safety_Tolerance::BITEXACT, const NumericalRecipe*>;
    using NT_RX = NumericalTier<safety_Tolerance::RELAXED, const NumericalRecipe*>;
    using NT_F16 = NumericalTier<safety_Tolerance::ULP_FP16, const NumericalRecipe*>;

    static_assert(numerical_tier_v<NT_BX>         == NT_BX::tier);
    static_assert(numerical_tier_v<NT_RX>         == NT_RX::tier);
    static_assert(numerical_tier_v<NT_F16>        == NT_F16::tier);
    static_assert(numerical_tier_v<NT_BX&>        == NT_BX::tier);
    static_assert(numerical_tier_v<NT_BX const&>  == NT_BX::tier);
    static_assert(numerical_tier_v<NT_BX&&>       == NT_BX::tier);

    // Concept gate — non-NumericalTier types rejected.
    static_assert(!is_numerical_tier_v<int>);
    static_assert(!is_numerical_tier_v<const NumericalRecipe*>);
    static_assert( is_numerical_tier_v<NT_BX>);
  }

  // ── T18 — by_name_pinned + by_hash_pinned API parity ──────────
  //
  // Both pinned variants must return THE SAME wrapped pointer when
  // resolving the same recipe.  Captures any drift between the two
  // lookup paths (e.g., by_hash bypassing tolerance verification or
  // returning a different recipe instance).
  // ═══════════════════════════════════════════════════════════════
  {
    auto base = reg.by_name(names::kF32Strict);
    assert(base.has_value());
    const RecipeHash hash = (*base)->hash;

    auto by_name_pin =
        reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Strict);
    auto by_hash_pin = reg.by_hash_pinned<safety_Tolerance::BITEXACT>(hash);
    assert(by_name_pin.has_value());
    assert(by_hash_pin.has_value());
    // Both paths produce the SAME canonical interned pointer.
    assert(by_name_pin->peek() == by_hash_pin->peek());
    assert(by_name_pin->peek() == *base);

    // Type-identity: both paths produce the same NumericalTier
    // instantiation.
    static_assert(std::is_same_v<
        decltype(by_name_pin)::value_type,
        decltype(by_hash_pin)::value_type>);
  }

  // ── T19 — Full 7×7 satisfies<> truth table ────────────────────
  //
  // Exhaustive verification of the lattice direction across all
  // (Self, Required) tier pairs.  Each cell = leq(Required, Self).
  // Lattice: RELAXED(0) ⊑ ULP_INT8 ⊑ ULP_FP8 ⊑ ULP_FP16 ⊑ ULP_FP32
  //          ⊑ ULP_FP64 ⊑ BITEXACT(6).
  // ═══════════════════════════════════════════════════════════════
  {
    using T = safety_Tolerance;
    using NT = crucible::safety::NumericalTier<T::BITEXACT, int>;
    (void) NT{};  // suppress unused warning; type only used in static_asserts

    // Row Self=BITEXACT (top): satisfies all seven below.
    using BX = NumericalTier<T::BITEXACT, int>;
    static_assert(BX::satisfies<T::BITEXACT>);
    static_assert(BX::satisfies<T::ULP_FP64>);
    static_assert(BX::satisfies<T::ULP_FP32>);
    static_assert(BX::satisfies<T::ULP_FP16>);
    static_assert(BX::satisfies<T::ULP_FP8>);
    static_assert(BX::satisfies<T::ULP_INT8>);
    static_assert(BX::satisfies<T::RELAXED>);

    // Row Self=ULP_FP64: satisfies FP64-and-below.
    using F64 = NumericalTier<T::ULP_FP64, int>;
    static_assert(!F64::satisfies<T::BITEXACT>);
    static_assert( F64::satisfies<T::ULP_FP64>);
    static_assert( F64::satisfies<T::ULP_FP32>);
    static_assert( F64::satisfies<T::ULP_FP16>);
    static_assert( F64::satisfies<T::ULP_FP8>);
    static_assert( F64::satisfies<T::ULP_INT8>);
    static_assert( F64::satisfies<T::RELAXED>);

    // Row Self=ULP_FP32: satisfies FP32-and-below.
    using F32 = NumericalTier<T::ULP_FP32, int>;
    static_assert(!F32::satisfies<T::BITEXACT>);
    static_assert(!F32::satisfies<T::ULP_FP64>);
    static_assert( F32::satisfies<T::ULP_FP32>);
    static_assert( F32::satisfies<T::ULP_FP16>);
    static_assert( F32::satisfies<T::ULP_FP8>);
    static_assert( F32::satisfies<T::ULP_INT8>);
    static_assert( F32::satisfies<T::RELAXED>);

    // Row Self=ULP_FP16: satisfies FP16-and-below.
    using F16 = NumericalTier<T::ULP_FP16, int>;
    static_assert(!F16::satisfies<T::BITEXACT>);
    static_assert(!F16::satisfies<T::ULP_FP64>);
    static_assert(!F16::satisfies<T::ULP_FP32>);
    static_assert( F16::satisfies<T::ULP_FP16>);
    static_assert( F16::satisfies<T::ULP_FP8>);
    static_assert( F16::satisfies<T::ULP_INT8>);
    static_assert( F16::satisfies<T::RELAXED>);

    // Row Self=ULP_FP8: satisfies FP8-and-below.
    using F8 = NumericalTier<T::ULP_FP8, int>;
    static_assert(!F8::satisfies<T::BITEXACT>);
    static_assert(!F8::satisfies<T::ULP_FP64>);
    static_assert(!F8::satisfies<T::ULP_FP32>);
    static_assert(!F8::satisfies<T::ULP_FP16>);
    static_assert( F8::satisfies<T::ULP_FP8>);
    static_assert( F8::satisfies<T::ULP_INT8>);
    static_assert( F8::satisfies<T::RELAXED>);

    // Row Self=ULP_INT8: satisfies INT8-and-RELAXED.
    using I8 = NumericalTier<T::ULP_INT8, int>;
    static_assert(!I8::satisfies<T::BITEXACT>);
    static_assert(!I8::satisfies<T::ULP_FP64>);
    static_assert(!I8::satisfies<T::ULP_FP32>);
    static_assert(!I8::satisfies<T::ULP_FP16>);
    static_assert(!I8::satisfies<T::ULP_FP8>);
    static_assert( I8::satisfies<T::ULP_INT8>);
    static_assert( I8::satisfies<T::RELAXED>);

    // Row Self=RELAXED (bottom): satisfies only RELAXED.
    using RX = NumericalTier<T::RELAXED, int>;
    static_assert(!RX::satisfies<T::BITEXACT>);
    static_assert(!RX::satisfies<T::ULP_FP64>);
    static_assert(!RX::satisfies<T::ULP_FP32>);
    static_assert(!RX::satisfies<T::ULP_FP16>);
    static_assert(!RX::satisfies<T::ULP_FP8>);
    static_assert(!RX::satisfies<T::ULP_INT8>);
    static_assert( RX::satisfies<T::RELAXED>);
  }

  // ── T20 — Move-only T witness (NumericalTier composition) ─────
  //
  // The pinned wrapper must compose with move-only payloads: the
  // production carrier T = const NumericalRecipe* is trivially
  // copyable, but the wrapper's algebraic foundation must support
  // T = MoveOnlyType for general composability.
  // ═══════════════════════════════════════════════════════════════
  {
    struct MoveOnlyT {
      int v{0};
      constexpr MoveOnlyT() = default;
      constexpr explicit MoveOnlyT(int x) : v{x} {}
      constexpr MoveOnlyT(MoveOnlyT&&) = default;
      constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
      MoveOnlyT(MoveOnlyT const&) = delete;
      MoveOnlyT& operator=(MoveOnlyT const&) = delete;
    };

    using NT_MO = NumericalTier<safety_Tolerance::BITEXACT, MoveOnlyT>;
    static_assert(!std::is_copy_constructible_v<NT_MO>);
    static_assert( std::is_move_constructible_v<NT_MO>);

    NT_MO src{MoveOnlyT{77}};
    auto relaxed = std::move(src).relax<safety_Tolerance::RELAXED>();
    static_assert(std::is_same_v<decltype(relaxed),
        NumericalTier<safety_Tolerance::RELAXED, MoveOnlyT>>);
    MoveOnlyT v = std::move(relaxed).consume();
    assert(v.v == 77);
  }

  // ── T21 — Idempotency: by_name_pinned twice with same args ────
  //
  // Calling the same registry with the same (name, T) twice must
  // produce IDENTICAL canonical pointers (RecipePool intern ensures
  // this).  Captures any accidental ordering / state-dependence in
  // the pinned lookup path.
  // ═══════════════════════════════════════════════════════════════
  {
    auto pin1 =
        reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Strict);
    auto pin2 =
        reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Strict);
    assert(pin1.has_value() && pin2.has_value());
    assert(pin1->peek() == pin2->peek());

    // Same name, DIFFERENT pin tier — both succeed (BITEXACT recipe
    // satisfies any weaker class), pointers identical, types
    // distinct.
    auto pin3 =
        reg.by_name_pinned<safety_Tolerance::ULP_FP32>(names::kF32Strict);
    assert(pin3.has_value());
    assert(pin3->peek() == pin1->peek());
    static_assert(!std::is_same_v<
        decltype(pin1)::value_type, decltype(pin3)::value_type>);

    // The error path is deterministic too: requesting a tier the
    // recipe cannot satisfy must produce ToleranceMismatch every
    // time, never a stale wrapper.
    auto bad1 = reg.by_name_pinned<safety_Tolerance::BITEXACT>(
        names::kF32Ordered);
    auto bad2 = reg.by_name_pinned<safety_Tolerance::BITEXACT>(
        names::kF32Ordered);
    assert(!bad1.has_value() && !bad2.has_value());
    assert(bad1.error() == RecipeError::ToleranceMismatch);
    assert(bad2.error() == RecipeError::ToleranceMismatch);
  }

  std::puts("ok");
  return 0;
}
