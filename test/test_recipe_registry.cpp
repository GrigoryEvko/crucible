// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// ═══════════════════════════════════════════════════════════════════
// test_recipe_registry — Layer 3 of FORGE.md §19/§20 recipe subsystem.
//
// Exercises the 8-starter-recipe registry: starter specs intern into
// a RecipePool at construction, by_name lookup returns the canonical
// interned pointer, entries() enumerate the full set, hashes are
// Family-A stable.
//
// Layer 1 coverage: test_numerical_recipe.cpp (compute_recipe_hash).
// Layer 2 coverage: test_recipe_pool.cpp (intern / growth / identity).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>

#include "test_assert.h"
#include <cinttypes>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace {

using crucible::Arena;
using crucible::NumericalRecipe;
using crucible::RecipeError;
using crucible::RecipePool;
using crucible::RecipeRegistry;
using crucible::ReductionAlgo;
using crucible::ReductionDeterminism;
using crucible::RoundingMode;
using crucible::ScalarType;
using crucible::ScalePolicy;
using crucible::SoftmaxRecurrence;

namespace names = crucible::recipe_names;

crucible::effects::Test g_test{};
inline crucible::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }

} // namespace

int main() {
  // ═══════════════════════════════════════════════════════════════════
  // 0. Compile-time axiom checks
  // ═══════════════════════════════════════════════════════════════════
  {
    // MemSafe: interior pointers into pool_→arena must not be copied
    // or moved.
    static_assert(!std::is_copy_constructible_v<RecipeRegistry>);
    static_assert(!std::is_copy_assignable_v<RecipeRegistry>);
    static_assert(!std::is_move_constructible_v<RecipeRegistry>);
    static_assert(!std::is_move_assignable_v<RecipeRegistry>);

    // TypeSafe: starter count is a static compile-time constant.
    static_assert(RecipeRegistry::STARTER_COUNT == 8);
    static_assert(RecipeRegistry::size() == 8);

    // Starter specs fully specified at compile time.
    static_assert(
        crucible::detail_recipe_registry::kStarterRecipes.size()
        == RecipeRegistry::STARTER_COUNT);
  }

  // ═══════════════════════════════════════════════════════════════════
  // 1. Construction populates every entry; interned pointers are non-null
  //    and the pool reports exactly STARTER_COUNT entries.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry reg{pool, alloc_cap()};

    assert(reg.entries().size() == RecipeRegistry::STARTER_COUNT);
    assert(pool.size() == RecipeRegistry::STARTER_COUNT);

    for (const auto& e : reg.entries()) {
      assert(!e.name.empty());
      assert(e.recipe != nullptr);
      // Every interned recipe has a populated Family-A hash.
      assert(e.recipe->hash
             == crucible::compute_recipe_hash(*e.recipe));
      // Every starter recipe uses the ubiquitous FP32 accumulator.
      assert(e.recipe->accum_dtype == ScalarType::Float);
    }
  }

  // ═══════════════════════════════════════════════════════════════════
  // 2. by_name hits every registered starter and returns the same
  //    canonical pointer as the corresponding entries() row.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry reg{pool, alloc_cap()};

    // Collect expected (name → recipe*) from entries().
    std::unordered_set<const NumericalRecipe*> entry_ptrs;
    for (const auto& e : reg.entries()) {
      entry_ptrs.insert(e.recipe);
    }
    assert(entry_ptrs.size() == RecipeRegistry::STARTER_COUNT);

    const std::string_view known_names[] = {
        names::kF32Strict,
        names::kF32Ordered,
        names::kF16F32AccumTc,
        names::kF16F32AccumOrdered,
        names::kBf16F32AccumTc,
        names::kBf16F32AccumOrdered,
        names::kFp8E4m3F32AccumMxOrd,
        names::kFp8E5m2F32AccumMxOrd,
    };

    for (auto n : known_names) {
      auto r = reg.by_name(n);
      assert(r.has_value() && "starter recipe lookup should hit");
      assert(*r != nullptr);
      // The pointer must be one of the pool-interned entries.
      assert(entry_ptrs.contains(*r));
    }

    // Two by_name calls for the same name return the same pointer.
    auto first  = reg.by_name(names::kF16F32AccumTc);
    auto second = reg.by_name(names::kF16F32AccumTc);
    assert(first.has_value() && second.has_value());
    assert(*first == *second);
  }

  // ═══════════════════════════════════════════════════════════════════
  // 3. Unknown name returns RecipeError::NameNotFound
  //
  //    by_name is case-sensitive and exact; verify a near-miss,
  //    a completely bogus name, and an empty name all miss.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry reg{pool, alloc_cap()};

    const std::string_view missing[] = {
        "F32_STRICT",                    // wrong case
        "f32_Strict",                    // mixed case
        "f32-strict",                    // dash instead of underscore
        "f32_strict ",                   // trailing whitespace
        " f32_strict",                   // leading whitespace
        "",                              // empty
        "this_recipe_does_not_exist",    // bogus
        "f32",                           // truncated
        "f32_strict_unknown_suffix",     // extended
    };

    for (auto n : missing) {
      auto r = reg.by_name(n);
      assert(!r.has_value() && "missing recipe must not hit");
      assert(r.error() == RecipeError::NameNotFound);
    }
  }

  // ═══════════════════════════════════════════════════════════════════
  // 4. Field-level correctness of each starter recipe
  //
  //    FORGE.md §20.1 specifies the composition of each recipe; verify
  //    each is what it claims to be.  Any refactor that swaps dtypes
  //    or determinism tiers on an existing name would break every
  //    downstream consumer that pinned the recipe.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry reg{pool, alloc_cap()};

    {
      auto r = reg.by_name(names::kF32Strict);
      assert(r.has_value());
      const auto& x = **r;
      assert(x.accum_dtype == ScalarType::Float);
      assert(x.out_dtype   == ScalarType::Float);
      assert(x.determinism == ReductionDeterminism::BITEXACT_STRICT);
      assert(x.scale_policy == ScalePolicy::NONE);
      assert(x.softmax == SoftmaxRecurrence::ONLINE_LSE);
      assert(crucible::is_bitexact(x.determinism));
      // BITEXACT_STRICT → no tensor cores
      assert(!crucible::permits_tensor_cores(x.determinism));
    }

    {
      auto r = reg.by_name(names::kF16F32AccumTc);
      assert(r.has_value());
      const auto& x = **r;
      assert(x.accum_dtype == ScalarType::Float);
      assert(x.out_dtype   == ScalarType::Half);
      assert(x.determinism == ReductionDeterminism::BITEXACT_TC);
      assert(crucible::is_bitexact(x.determinism));
      assert(crucible::permits_tensor_cores(x.determinism));
    }

    {
      auto r = reg.by_name(names::kBf16F32AccumTc);
      assert(r.has_value());
      const auto& x = **r;
      assert(x.accum_dtype == ScalarType::Float);
      assert(x.out_dtype   == ScalarType::BFloat16);
      assert(x.determinism == ReductionDeterminism::BITEXACT_TC);
    }

    {
      auto r = reg.by_name(names::kFp8E4m3F32AccumMxOrd);
      assert(r.has_value());
      const auto& x = **r;
      assert(x.accum_dtype  == ScalarType::Float);
      assert(x.out_dtype    == ScalarType::Float8_e4m3fn);
      assert(x.scale_policy == ScalePolicy::PER_BLOCK_MX);
      assert(x.softmax      == SoftmaxRecurrence::NAIVE);
      assert(x.determinism  == ReductionDeterminism::ORDERED);
      // Block-scaled formats cannot be BITEXACT (per FORGE.md §19.1).
      assert(!crucible::is_bitexact(x.determinism));
      assert(crucible::allows_block_scaled_formats(x.determinism));
    }

    {
      auto r = reg.by_name(names::kFp8E5m2F32AccumMxOrd);
      assert(r.has_value());
      const auto& x = **r;
      assert(x.out_dtype == ScalarType::Float8_e5m2);
      assert(x.scale_policy == ScalePolicy::PER_BLOCK_MX);
      assert(x.determinism == ReductionDeterminism::ORDERED);
    }
  }

  // ═══════════════════════════════════════════════════════════════════
  // 5. All starter names are distinct
  //
  //    Name collision would silently mask a recipe (the first match
  //    wins in by_name's linear scan).  Proactively verify.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry reg{pool, alloc_cap()};

    std::unordered_set<std::string_view> seen;
    for (const auto& e : reg.entries()) {
      const auto [it, inserted] = seen.insert(e.name);
      assert(inserted && "starter recipe name collision");
    }
    assert(seen.size() == RecipeRegistry::STARTER_COUNT);
  }

  // ═══════════════════════════════════════════════════════════════════
  // 6. All starter recipes have distinct semantic fields (⇒ distinct
  //    Family-A hashes ⇒ distinct pool-interned pointers).
  //
  //    If two starter specs accidentally collapse (e.g., a copy-paste
  //    error leaves two recipes with identical fields), the pool
  //    would intern them to the same pointer and pool.size() would
  //    be < STARTER_COUNT.  This catches that drift.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry reg{pool, alloc_cap()};

    std::unordered_set<const NumericalRecipe*> ptrs;
    std::unordered_set<uint64_t> hashes;

    for (const auto& e : reg.entries()) {
      const auto [it_p, inserted_p] = ptrs.insert(e.recipe);
      assert(inserted_p && "two starter recipes intern to the same pointer");
      const auto [it_h, inserted_h] = hashes.insert(e.recipe->hash.raw());
      assert(inserted_h && "two starter recipes share a Family-A hash");
    }

    assert(ptrs.size() == RecipeRegistry::STARTER_COUNT);
    assert(hashes.size() == RecipeRegistry::STARTER_COUNT);
  }

  // ═══════════════════════════════════════════════════════════════════
  // 7. Family-A golden hashes for the starter set
  //
  //    Every starter recipe's `compute_recipe_hash` output is pinned
  //    here.  Drift in any value means either:
  //      - someone edited the semantic fields of the starter recipe
  //        (possibly unintentionally), or
  //      - someone changed compute_recipe_hash's fold scheme.
  //
  //    Either is a wire-format break across every persisted Cipher
  //    entry that referenced the recipe by hash.  The goldens are
  //    the CI tripwire.
  //
  //    Update procedure (only after an AUDITED intentional change):
  //      1. Run this test, capture printed actual hashes
  //      2. Replace the EXPECTED_* constants below
  //      3. Bump CDAG_VERSION if any Cipher entries already persisted
  //         the old hashes
  //      4. Update crucible/data/recipes.json (when it exists)
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry reg{pool, alloc_cap()};

    struct Golden {
      std::string_view name;
      uint64_t expected;
    };
    const Golden goldens[] = {
        // Captured on initial implementation run; pinned by CI.
        // Cross-pinned with test_numerical_recipe.cpp for overlap
        // between the two test surfaces (f32_strict, f16_f32accum_tc,
        // fp8e4m3_f32accum_mx_ordered) — both must agree or CI fires.
        {names::kF32Strict,             0xce0eb0cd5c376b79ULL},
        {names::kF32Ordered,            0x493a001a4ad235a7ULL},
        {names::kF16F32AccumTc,         0xc737d38ea930d024ULL},
        {names::kF16F32AccumOrdered,    0xcc51530cf248c3e3ULL},
        {names::kBf16F32AccumTc,        0x0acf66d27c444494ULL},
        {names::kBf16F32AccumOrdered,   0xc674007dc8716618ULL},
        {names::kFp8E4m3F32AccumMxOrd,  0x5ba4c6b1bdefc89dULL},
        {names::kFp8E5m2F32AccumMxOrd,  0xb8e80f767e9cfb21ULL},
    };

    bool any_drift = false;
    for (auto g : goldens) {
      auto r = reg.by_name(g.name);
      assert(r.has_value());
      const uint64_t actual = (*r)->hash.raw();
      if (actual != g.expected) {
        std::fprintf(stderr,
            "recipe-hash golden DRIFT for %.*s: expected 0x%016" PRIx64
            ", got 0x%016" PRIx64 "\n",
            int(g.name.size()), g.name.data(),
            g.expected, actual);
        any_drift = true;
      }
    }
    if (any_drift) {
      std::fprintf(stderr,
          "  → update EXPECTED_* constants in test_recipe_registry.cpp\n"
          "  → audit every persisted consumer of the drifted hashes\n"
          "  → consider bumping CDAG_VERSION\n");
      assert(!any_drift && "recipe-hash golden mismatch");
    }
  }

  // ═══════════════════════════════════════════════════════════════════
  // 8. Pool-registry integration: manually interning the starter
  //    fields into the SAME pool yields the same canonical pointer
  //    as by_name.  This is the core pool-authority invariant.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry reg{pool, alloc_cap()};

    for (const auto& spec :
         crucible::detail_recipe_registry::kStarterRecipes) {
      // Re-intern the starter spec's fields → must hit the canonical
      // pointer already stored by the registry.
      const auto* via_pool = pool.intern(alloc_cap(), spec.fields);
      auto via_reg = reg.by_name(spec.name);
      assert(via_reg.has_value());
      assert(via_pool == *via_reg);
    }
    // No new pool entries created — every intern was a hit.
    assert(pool.size() == RecipeRegistry::STARTER_COUNT);
  }

  // ═══════════════════════════════════════════════════════════════════
  // 9. by_hash — the Cipher load-path counterpart to by_name
  //
  //    Persisted RecipeHash values must resolve back to the canonical
  //    const NumericalRecipe* pointer.  Three cases:
  //      (a) hit — every starter's stored hash resolves to the same
  //          pointer as by_name
  //      (b) miss — bogus hash returns HashNotFound (NOT
  //          NameNotFound; the two error classes are distinct)
  //      (c) sentinel miss — RecipeHash::sentinel() (UINT64_MAX)
  //          is reserved as an end-of-region marker and must never
  //          appear as a valid recipe hash
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry reg{pool, alloc_cap()};

    // (a) Every starter's hash resolves via by_hash to the same
    //     canonical pointer as by_name.
    for (const auto& entry : reg.entries()) {
      auto via_hash = reg.by_hash(entry.recipe->hash);
      assert(via_hash.has_value());
      assert(*via_hash == entry.recipe);

      auto via_name = reg.by_name(entry.name);
      assert(via_name.has_value());
      assert(*via_hash == *via_name);
    }

    // (b) Bogus hashes miss with HashNotFound — NOT NameNotFound.
    //     The two error classes are distinct so callers can
    //     distinguish "user typo" (by_name miss) from "Cipher
    //     downgrade" (by_hash miss).
    const crucible::RecipeHash bogus_hashes[] = {
        crucible::RecipeHash{0xDEADBEEFCAFEBABEULL},
        crucible::RecipeHash{0x0000000000000000ULL},  // all-zero
        crucible::RecipeHash{0x0000000000000001ULL},  // near-zero
        crucible::RecipeHash{0xFFFFFFFFFFFFFFFEULL},  // near-max
    };
    for (auto h : bogus_hashes) {
      auto r = reg.by_hash(h);
      assert(!r.has_value());
      assert(r.error() == RecipeError::HashNotFound);
    }

    // (c) Sentinel hash (UINT64_MAX) is reserved as end-of-region
    //     marker in RegionNode (see Types.h RecipeHash::sentinel).
    //     A real recipe must never produce it.
    auto sentinel_miss = reg.by_hash(crucible::RecipeHash::sentinel());
    assert(!sentinel_miss.has_value());
    assert(sentinel_miss.error() == RecipeError::HashNotFound);
  }

  // ═══════════════════════════════════════════════════════════════════
  // 10. Independence across registries — two registries, two pools,
  //     same names, different pointers; same Family-A hashes.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena_a{};
    Arena arena_b{};
    RecipePool pool_a{arena_a, alloc_cap()};
    RecipePool pool_b{arena_b, alloc_cap()};
    RecipeRegistry reg_a{pool_a, alloc_cap()};
    RecipeRegistry reg_b{pool_b, alloc_cap()};

    auto a = reg_a.by_name(names::kF16F32AccumTc);
    auto b = reg_b.by_name(names::kF16F32AccumTc);
    assert(a.has_value() && b.has_value());
    assert(*a != *b);                           // different pools
    assert((*a)->hash == (*b)->hash);           // same Family-A hash
    assert((*a)->out_dtype == (*b)->out_dtype); // same semantics
  }

  std::printf("test_recipe_registry: all tests passed\n");
  return 0;
}
