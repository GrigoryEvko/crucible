// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// ═══════════════════════════════════════════════════════════════════
// test_recipe_pool — Layer 2 of FORGE.md §19/§20 recipe subsystem.
//
// Exercises the RecipePool Swiss-table interning substrate.  Scope:
//
//   - Intern identity (same semantic fields → same const NumericalRecipe*)
//   - Hash-field authority (pool computes, caller's hash input ignored)
//   - Semantic equality ignores the stored `hash` byte-for-byte
//   - Growth preserves pointer identity (arena-owned recipes don't move)
//   - Size/capacity accounting under insert + grow
//   - Non-null return contract (gnu::returns_nonnull)
//   - Copy/move deleted at compile time
//
// Layer 1 (compute_recipe_hash Family-A stability) is covered by
// test_numerical_recipe.cpp.  Layer 3 (RecipeRegistry starter set)
// will be covered by test_recipe_registry.cpp.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Arena.h>
#include <crucible/Effects.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <type_traits>
#include <unordered_set>

namespace {

using crucible::Arena;
using crucible::NumericalRecipe;
using crucible::RecipeHash;
using crucible::RecipePool;
using crucible::ReductionAlgo;
using crucible::ReductionDeterminism;
using crucible::RoundingMode;
using crucible::ScalarType;
using crucible::ScalePolicy;
using crucible::SoftmaxRecurrence;

// Construct a recipe from (accum, out, determinism) with other fields
// at NSDMI defaults.  Enough field diversity to avoid hash collisions
// across small enumerated test matrices.
[[nodiscard]] constexpr NumericalRecipe mk(
    ScalarType accum, ScalarType out,
    ReductionDeterminism det,
    ScalePolicy scale = ScalePolicy::NONE,
    uint8_t flags = 0) noexcept
{
  NumericalRecipe r{};
  r.accum_dtype  = accum;
  r.out_dtype    = out;
  r.determinism  = det;
  r.scale_policy = scale;
  r.flags        = flags;
  return r;
}

// The fx::Alloc capability token has a private default constructor; only
// fx::{Bg, Init, Test} are friended to mint one.  Tests use fx::Test.
crucible::fx::Test g_test{};
inline crucible::fx::Alloc alloc_cap() noexcept { return g_test.alloc; }

} // namespace

int main() {
  // ═══════════════════════════════════════════════════════════════════
  // 0. Compile-time axiom checks
  // ═══════════════════════════════════════════════════════════════════
  {
    // MemSafe: copies/moves are deleted.  Interior pointers into arena_
    // would dangle if the pool were relocated.
    static_assert(!std::is_copy_constructible_v<RecipePool>,
                  "RecipePool must not be copy-constructible");
    static_assert(!std::is_copy_assignable_v<RecipePool>,
                  "RecipePool must not be copy-assignable");
    static_assert(!std::is_move_constructible_v<RecipePool>,
                  "RecipePool must not be move-constructible");
    static_assert(!std::is_move_assignable_v<RecipePool>,
                  "RecipePool must not be move-assignable");
  }

  // ═══════════════════════════════════════════════════════════════════
  // 1. Empty pool sanity
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap(), 32};
    assert(pool.size() == 0);
    assert(pool.capacity() == 32);
  }

  // ═══════════════════════════════════════════════════════════════════
  // 2. Intern identity — same semantic fields → same pointer
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap(), 32};

    const NumericalRecipe fields = mk(
        ScalarType::Float, ScalarType::Half,
        ReductionDeterminism::BITEXACT_TC);

    const auto* a = pool.intern(alloc_cap(), fields);
    const auto* b = pool.intern(alloc_cap(), fields);
    assert(a == b);
    assert(pool.size() == 1);

    // The returned pointer is non-null, recipe hash is authoritative
    // (matches compute_recipe_hash), semantic fields match input.
    assert(a != nullptr);
    assert(a->hash == crucible::compute_recipe_hash(fields));
    assert(a->accum_dtype == ScalarType::Float);
    assert(a->out_dtype == ScalarType::Half);
    assert(a->determinism == ReductionDeterminism::BITEXACT_TC);
  }

  // ═══════════════════════════════════════════════════════════════════
  // 3. Different fields → different pointers (no aliasing)
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap(), 32};

    const auto* r_f32_strict = pool.intern(alloc_cap(),
        mk(ScalarType::Float, ScalarType::Float,
           ReductionDeterminism::BITEXACT_STRICT));
    const auto* r_f16_tc = pool.intern(alloc_cap(),
        mk(ScalarType::Float, ScalarType::Half,
           ReductionDeterminism::BITEXACT_TC));
    const auto* r_bf16_ordered = pool.intern(alloc_cap(),
        mk(ScalarType::Float, ScalarType::BFloat16,
           ReductionDeterminism::ORDERED));

    assert(r_f32_strict != r_f16_tc);
    assert(r_f32_strict != r_bf16_ordered);
    assert(r_f16_tc != r_bf16_ordered);
    assert(pool.size() == 3);
  }

  // ═══════════════════════════════════════════════════════════════════
  // 4. Hash-field authority — input's hash is ignored, pool writes its own
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap(), 32};

    NumericalRecipe poisoned = mk(
        ScalarType::Float, ScalarType::Half,
        ReductionDeterminism::ORDERED);
    poisoned.hash = RecipeHash{0xDEADBEEFCAFEBABEULL};

    const auto* r = pool.intern(alloc_cap(), poisoned);
    // Stored hash is compute_recipe_hash of the semantic fields,
    // NOT the poisoned value supplied by the caller.
    assert(r->hash == crucible::compute_recipe_hash(poisoned));
    assert(r->hash != RecipeHash{0xDEADBEEFCAFEBABEULL});
  }

  // ═══════════════════════════════════════════════════════════════════
  // 5. Semantic equality ignores the stored hash
  //
  // Interning fresh fields, then interning the SAME semantic fields
  // with a poisoned hash, must return the exact same pointer.  This
  // verifies the probing comparison operates on the 8 semantic bytes
  // only; a future refactor that folds the hash byte into the
  // comparison would break this test.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap(), 32};

    NumericalRecipe fresh = mk(
        ScalarType::Float, ScalarType::Float,
        ReductionDeterminism::BITEXACT_STRICT);
    const auto* a = pool.intern(alloc_cap(), fresh);

    NumericalRecipe poisoned = fresh;
    poisoned.hash = RecipeHash{0xFFFFFFFFFFFFFFFFULL};
    const auto* b = pool.intern(alloc_cap(), poisoned);

    assert(a == b);
    assert(pool.size() == 1);
  }

  // ═══════════════════════════════════════════════════════════════════
  // 6. Growth preserves pointer identity
  //
  // Arena-owned recipes never move.  Interning the same semantic
  // fields before and after a grow must return the same const
  // NumericalRecipe*.  The grow_() call rehashes slot entries into
  // a doubled table; pointers to arena-owned recipes are unaffected.
  //
  // Capacity 8 → 50% load = 4 entries before first grow.  We insert
  // 5 distinct recipes to force one grow, then 5 more to force a
  // second grow (capacity 16 → 32), and verify all 10 pointers
  // remain stable across both grows.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap(), 8};
    assert(pool.capacity() == 8);

    constexpr unsigned N = 10;
    const NumericalRecipe keys[N] = {
        mk(ScalarType::Float,         ScalarType::Float,        ReductionDeterminism::BITEXACT_STRICT),
        mk(ScalarType::Float,         ScalarType::Half,         ReductionDeterminism::BITEXACT_TC),
        mk(ScalarType::Float,         ScalarType::BFloat16,     ReductionDeterminism::BITEXACT_TC),
        mk(ScalarType::Float,         ScalarType::Half,         ReductionDeterminism::ORDERED),
        mk(ScalarType::Float,         ScalarType::BFloat16,     ReductionDeterminism::ORDERED),
        mk(ScalarType::Float,         ScalarType::Float8_e4m3fn, ReductionDeterminism::ORDERED, ScalePolicy::PER_BLOCK_MX),
        mk(ScalarType::Float,         ScalarType::Float8_e5m2,  ReductionDeterminism::ORDERED, ScalePolicy::PER_BLOCK_MX),
        mk(ScalarType::Float,         ScalarType::Half,         ReductionDeterminism::UNORDERED),
        mk(ScalarType::Float,         ScalarType::BFloat16,     ReductionDeterminism::UNORDERED),
        mk(ScalarType::Float,         ScalarType::Float,        ReductionDeterminism::ORDERED),
    };

    const NumericalRecipe* ptrs[N]{};

    // Initial insert — forces two grows (8 → 16 → 32).
    for (unsigned i = 0; i < N; ++i) {
      ptrs[i] = pool.intern(alloc_cap(), keys[i]);
      assert(ptrs[i] != nullptr);
    }

    // The pool must have grown at least twice to accommodate 10
    // entries under 50% load.  Actual capacity after grows: 32.
    assert(pool.capacity() >= 32);
    assert(pool.size() == N);

    // Every pointer is unique — no aliasing across the grows.
    for (unsigned i = 0; i < N; ++i) {
      for (unsigned j = i + 1; j < N; ++j) {
        assert(ptrs[i] != ptrs[j]);
      }
    }

    // Re-interning each key returns the same pointer captured
    // before the grows — arena-owned recipes are address-stable.
    for (unsigned i = 0; i < N; ++i) {
      const auto* re = pool.intern(alloc_cap(), keys[i]);
      assert(re == ptrs[i]);
    }
    assert(pool.size() == N);  // no new entries on hits
  }

  // ═══════════════════════════════════════════════════════════════════
  // 7. Dense grid interning — 400 distinct recipes with no aliasing
  //
  // Enumerate the 5×5×4×4 = 400 dtype × dtype × determinism × scale
  // grid that test_numerical_recipe already verified produces 400
  // distinct hashes.  Interning each must yield 400 distinct
  // const NumericalRecipe* pointers, and re-interning each key must
  // hit the same pointer.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap(), 32};

    const ScalarType dtypes[] = {
        ScalarType::Float, ScalarType::Half, ScalarType::BFloat16,
        ScalarType::Float8_e4m3fn, ScalarType::Float8_e5m2,
    };
    const ReductionDeterminism dets[] = {
        ReductionDeterminism::UNORDERED,
        ReductionDeterminism::ORDERED,
        ReductionDeterminism::BITEXACT_TC,
        ReductionDeterminism::BITEXACT_STRICT,
    };
    const ScalePolicy scales[] = {
        ScalePolicy::NONE,
        ScalePolicy::PER_TENSOR_POST,
        ScalePolicy::PER_BLOCK_MX,
        ScalePolicy::PER_BLOCK_NVFP4,
    };

    std::unordered_set<const NumericalRecipe*> seen_ptrs;
    std::unordered_set<uint64_t> seen_hashes;

    for (auto accum : dtypes) {
      for (auto out : dtypes) {
        for (auto det : dets) {
          for (auto sp : scales) {
            const NumericalRecipe fields = mk(accum, out, det, sp);
            const auto* p = pool.intern(alloc_cap(), fields);
            const auto [it_p, inserted_p] = seen_ptrs.insert(p);
            assert(inserted_p && "pool aliased two distinct recipes");
            const auto [it_h, inserted_h] = seen_hashes.insert(p->hash.raw());
            assert(inserted_h && "two distinct recipes share a hash");
          }
        }
      }
    }

    assert(pool.size() == 400);
    assert(seen_ptrs.size() == 400);
    assert(pool.capacity() >= 1024);  // 400 @ 50% load requires capacity >= 800 → next pow2
  }

  // ═══════════════════════════════════════════════════════════════════
  // 8. Multiple pools are independent
  //
  // Two pools may intern the same semantic fields and return
  // different pointers — each pool owns its own arena-allocated
  // recipe storage.  Pointer equality is a per-pool invariant,
  // not a global invariant.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena_a{};
    Arena arena_b{};
    RecipePool pool_a{arena_a, alloc_cap(), 32};
    RecipePool pool_b{arena_b, alloc_cap(), 32};

    const NumericalRecipe fields = mk(
        ScalarType::Float, ScalarType::Float,
        ReductionDeterminism::BITEXACT_STRICT);

    const auto* a = pool_a.intern(alloc_cap(), fields);
    const auto* b = pool_b.intern(alloc_cap(), fields);

    assert(a != b);                             // different pools
    assert(a->hash == b->hash);                 // same Family-A hash
    assert(a->accum_dtype == b->accum_dtype);   // same semantics
  }

  // ═══════════════════════════════════════════════════════════════════
  // 9. Recipe const correctness — type system rejects mutation
  //
  // The returned pointer is `const NumericalRecipe*`; any attempt
  // to modify through it is a compile error.  Verified at compile
  // time only — no runtime assertion needed.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap(), 32};
    const auto* r = pool.intern(alloc_cap(),
        mk(ScalarType::Float, ScalarType::Float,
           ReductionDeterminism::ORDERED));
    static_assert(std::is_same_v<decltype(r), const NumericalRecipe*>,
                  "RecipePool::intern must return const NumericalRecipe*");
    assert(r != nullptr);
  }

  std::printf("test_recipe_pool: all tests passed\n");
  return 0;
}
