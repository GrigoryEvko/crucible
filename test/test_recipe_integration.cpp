// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// ═══════════════════════════════════════════════════════════════════
// test_recipe_integration — capstone for the Layer 1-3c recipe stack.
//
// Each scenario corresponds to a specific bug class the integration
// prevents.  Granular tests for each layer live in their own files
// (test_numerical_recipe, test_recipe_pool, test_recipe_registry,
// test_merkle_dag); this file proves the layers compose correctly.
//
//   Scenario                   Bug class prevented
//   ─────────────────────────  ──────────────────────────────────────
//   1. Pool independence       cross-process pool aliasing
//   2. Recipe disambiguation   silent cross-recipe content-hash collision
//   3. Cipher round-trip       lost recipe identity on Cipher recovery
//   4. Cross-process determin. divergent content_hashes in fresh process
//   5. Cache disambiguation    stale-recipe kernel served from cache
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/MerkleDag.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>

#include "test_assert.h"
#include <cstdio>

namespace {

using crucible::Arena;
using crucible::CompiledKernel;
using crucible::ContentHash;
using crucible::KernelCache;
using crucible::NumericalRecipe;
using crucible::RecipeHash;
using crucible::RecipePool;
using crucible::RecipeRegistry;
using crucible::SchemaHash;
using crucible::TraceEntry;

namespace names = crucible::recipe_names;

crucible::effects::Test g_test{};
inline crucible::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }

// Build a small but distinctive op trace; hash inputs are deterministic
// so cross-process / cross-pool comparisons remain stable.
//
// Returns the count as a strong-typed uint32_t to satisfy the
// TypeSafe -Werror=conversion guard at make_region call sites.
constexpr uint32_t kOpsCount = 3;
inline void mk_ops(TraceEntry (&buf)[kOpsCount]) noexcept {
  buf[0].schema_hash = SchemaHash{0xAA01};
  buf[1].schema_hash = SchemaHash{0xBB02};
  buf[2].schema_hash = SchemaHash{0xCC03};
}

}  // namespace

int main() {
  // ═══════════════════════════════════════════════════════════════════
  // Scenario 1 — Pool independence with hash identity
  //
  // BUG IT PREVENTS: aliasing a pool across processes.  Naive
  // implementations might assume "same recipe name → same pointer
  // everywhere", which fails the moment a fresh process starts up.
  // The right invariant: pointer identity is per-pool; CROSS-pool
  // identity is via Family-A hash.
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

    // Pointer identity is per-pool: different pools, different addrs.
    assert(*a != *b);
    // Family-A hash identity is global: same name → same hash.
    assert((*a)->hash == (*b)->hash);
    // Semantic field identity is global.
    assert((*a)->accum_dtype == (*b)->accum_dtype);
    assert((*a)->out_dtype   == (*b)->out_dtype);
    assert((*a)->determinism == (*b)->determinism);
  }

  // ═══════════════════════════════════════════════════════════════════
  // Scenario 2 — Recipe disambiguation in content_hash
  //
  // BUG IT PREVENTS: silent cross-recipe content-hash collision.
  // Two regions with byte-identical ops but different recipes MUST
  // hash differently; otherwise a kernel compiled under recipe A
  // would be served on a lookup pinned to recipe B, producing wrong
  // bits and breaking replay determinism (CRUCIBLE.md §10).
  //
  // The wire-in (compute_content_hash + make_region) must guarantee
  // hash inequality given different recipes.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry registry{pool, alloc_cap()};

    auto rec_tc     = registry.by_name(names::kF16F32AccumTc);
    auto rec_strict = registry.by_name(names::kF32Strict);
    assert(rec_tc.has_value() && rec_strict.has_value());

    TraceEntry ops[kOpsCount]{};
    mk_ops(ops);

    auto* region_tc     = crucible::make_region(alloc_cap(), arena,
        ops, kOpsCount, *rec_tc);
    auto* region_strict = crucible::make_region(alloc_cap(), arena,
        ops, kOpsCount, *rec_strict);
    auto* region_none   = crucible::make_region(alloc_cap(), arena,
        ops, kOpsCount);  // no-recipe overload

    // Three distinct content_hashes from three different recipe pins
    // (including the "no recipe" baseline).
    assert(region_tc->content_hash     != region_strict->content_hash);
    assert(region_tc->content_hash     != region_none->content_hash);
    assert(region_strict->content_hash != region_none->content_hash);

    // The three hashes are all non-zero (degenerate collapse check).
    assert(region_tc->content_hash.raw()     != 0);
    assert(region_strict->content_hash.raw() != 0);
    assert(region_none->content_hash.raw()   != 0);
  }

  // ═══════════════════════════════════════════════════════════════════
  // Scenario 3 — Cipher round-trip via by_hash
  //
  // BUG IT PREVENTS: lost recipe identity on Cipher recovery.  When
  // a Cipher-persisted blob is loaded in a recovery process, the
  // recipe identity must be recoverable from the persisted hash;
  // otherwise the recovered weights are silently mis-typed against
  // the recipe assumed by the loader.
  //
  // The recovery path: by_hash(persisted_hash) → canonical interned
  // pointer in the current process's registry.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry registry{pool, alloc_cap()};

    // "Persistence-time" — the original training process pins a
    // recipe and computes a region's content_hash.
    auto pin = registry.by_name(names::kBf16F32AccumTc);
    assert(pin.has_value());
    const NumericalRecipe* recipe_orig = *pin;

    TraceEntry ops[kOpsCount]{};
    mk_ops(ops);
    auto* region_orig = crucible::make_region(alloc_cap(), arena,
        ops, kOpsCount, recipe_orig);
    const ContentHash persisted_content_hash = region_orig->content_hash;

    // What Cipher persists per region: (region's content_hash,
    // recipe's Family-A hash) — both 8-byte values, byte-stable
    // across processes.  In the current implementation only
    // content_hash is stored on RegionNode; the recipe hash would
    // come from a sibling field once Phase E adds it (or from a
    // Trainer-level manifest).
    const RecipeHash persisted_recipe_hash = recipe_orig->hash;

    // "Recovery-time" — same process for testing, but the API path
    // is identical to a fresh recovery process.
    auto recovered = registry.by_hash(persisted_recipe_hash);
    assert(recovered.has_value());
    // In-process recovery yields the SAME canonical pointer.
    assert(*recovered == recipe_orig);
    // The recovered recipe's hash matches the persisted hash.
    assert((*recovered)->hash == persisted_recipe_hash);

    // Reconstruct the region with the recovered recipe — same
    // content_hash as the original.  This proves the persistence
    // path is bit-stable end-to-end.
    auto* region_recovered = crucible::make_region(alloc_cap(), arena,
        ops, kOpsCount, *recovered);
    assert(region_recovered->content_hash == persisted_content_hash);
  }

  // ═══════════════════════════════════════════════════════════════════
  // Scenario 4 — Cross-process determinism
  //
  // BUG IT PREVENTS: a fresh process producing a different
  // content_hash for the "same" region.  Pointer identity changes
  // (different ASLR addresses for the new pool's storage), but
  // Family-A hash identity is invariant — and content_hash depends
  // on the Family-A hash, not the pointer.  Therefore content_hash
  // is also process-independent.
  //
  // This is the load-bearing property for distributed checkpoint
  // recovery (CRUCIBLE.md §10) and federated cache sharing (FORGE.md
  // §23.2).
  // ═══════════════════════════════════════════════════════════════════
  {
    // "Process A" — the original training process.
    Arena arena_a{};
    RecipePool pool_a{arena_a, alloc_cap()};
    RecipeRegistry reg_a{pool_a, alloc_cap()};
    auto rec_a = reg_a.by_name(names::kFp8E4m3F32AccumMxOrd);
    assert(rec_a.has_value());

    TraceEntry ops_a[kOpsCount]{};
    mk_ops(ops_a);
    auto* region_a = crucible::make_region(alloc_cap(), arena_a,
        ops_a, kOpsCount, *rec_a);
    const ContentHash hash_in_proc_a = region_a->content_hash;

    // "Process B" — a fresh process with completely independent
    // pool / registry / arena state.  Different ASLR-derived
    // pointers everywhere.
    Arena arena_b{};
    RecipePool pool_b{arena_b, alloc_cap()};
    RecipeRegistry reg_b{pool_b, alloc_cap()};
    auto rec_b = reg_b.by_name(names::kFp8E4m3F32AccumMxOrd);
    assert(rec_b.has_value());
    // Different pointers — proves the pools really are independent.
    assert(*rec_a != *rec_b);
    // Same Family-A hash — the global identity invariant.
    assert((*rec_a)->hash == (*rec_b)->hash);

    TraceEntry ops_b[kOpsCount]{};
    mk_ops(ops_b);
    auto* region_b = crucible::make_region(alloc_cap(), arena_b,
        ops_b, kOpsCount, *rec_b);
    const ContentHash hash_in_proc_b = region_b->content_hash;

    // The proof: cross-process content_hash equality despite zero
    // pointer overlap.  This makes Cipher-persisted hashes valid
    // cache keys in any recovery process.
    assert(hash_in_proc_a == hash_in_proc_b);
  }

  // ═══════════════════════════════════════════════════════════════════
  // Scenario 5 — KernelCache disambiguation
  //
  // BUG IT PREVENTS: a kernel compiled under recipe A served on a
  // cache lookup pinned to recipe B.  Without recipe-disambiguated
  // content_hashes, a KernelCache populated during recipe-A training
  // would silently return its A-compiled kernels when the user
  // restarted under recipe B — wrong numerics, no diagnostic.
  //
  // The defense: content_hash is recipe-aware → cache key is
  // recipe-aware → no cross-recipe pollution.
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry registry{pool, alloc_cap()};

    auto rec_tc       = registry.by_name(names::kF16F32AccumTc);
    auto rec_ord      = registry.by_name(names::kF16F32AccumOrdered);
    auto rec_bf16_tc  = registry.by_name(names::kBf16F32AccumTc);
    assert(rec_tc.has_value() && rec_ord.has_value() && rec_bf16_tc.has_value());

    TraceEntry ops[kOpsCount]{};
    mk_ops(ops);

    auto* region_tc      = crucible::make_region(alloc_cap(), arena,
        ops, kOpsCount, *rec_tc);
    auto* region_ord     = crucible::make_region(alloc_cap(), arena,
        ops, kOpsCount, *rec_ord);
    auto* region_bf16_tc = crucible::make_region(alloc_cap(), arena,
        ops, kOpsCount, *rec_bf16_tc);

    // Three distinct content_hashes — each recipe gets its own
    // cache slot.
    assert(region_tc->content_hash      != region_ord->content_hash);
    assert(region_tc->content_hash      != region_bf16_tc->content_hash);
    assert(region_ord->content_hash     != region_bf16_tc->content_hash);

    KernelCache cache;
    using crucible::RowHash;
    // FOUND-I05: lookup/insert keys are now (ContentHash, RowHash).
    // This test exercises recipe-disambiguation at the ContentHash
    // axis, so all calls use RowHash{0} (the bare-type baseline).
    // Recipe orthogonality vs row-orthogonality is by-design — both
    // axes independently partition the cache.

    // "Train" under f16_tc: insert a fake compiled kernel.
    struct FakeKernel { const char* tag; };
    FakeKernel kernel_tc{"f16_tc-compiled"};
    auto ins = cache.insert(region_tc->content_hash, RowHash{0},
        reinterpret_cast<CompiledKernel*>(&kernel_tc));
    assert(ins.has_value());
    assert(cache.lookup(region_tc->content_hash, RowHash{0}) ==
           reinterpret_cast<CompiledKernel*>(&kernel_tc));

    // "Switch to f16_ordered" — lookup under that recipe's
    // content_hash MUST miss the f16_tc-compiled kernel.  Without
    // recipe disambiguation, both regions would share a cache slot
    // and the f16_tc kernel would silently serve f16_ordered work.
    assert(cache.lookup(region_ord->content_hash, RowHash{0}) == nullptr);

    // "Switch to bf16_tc" — same isolation property.
    assert(cache.lookup(region_bf16_tc->content_hash, RowHash{0}) == nullptr);

    // "Switch back to f16_tc" — original kernel still served.
    assert(cache.lookup(region_tc->content_hash, RowHash{0}) ==
           reinterpret_cast<CompiledKernel*>(&kernel_tc));

    // Insert a separate kernel under f16_ordered's content_hash,
    // verify both kernels coexist.  Demonstrates that recipe-
    // disambiguation populates the cache key space cleanly with
    // no aliasing across recipes.
    FakeKernel kernel_ord{"f16_ordered-compiled"};
    auto ins2 = cache.insert(region_ord->content_hash, RowHash{0},
        reinterpret_cast<CompiledKernel*>(&kernel_ord));
    assert(ins2.has_value());
    assert(cache.lookup(region_tc->content_hash, RowHash{0}) ==
           reinterpret_cast<CompiledKernel*>(&kernel_tc));
    assert(cache.lookup(region_ord->content_hash, RowHash{0}) ==
           reinterpret_cast<CompiledKernel*>(&kernel_ord));
    // No cross-pollution.
    assert(cache.lookup(region_bf16_tc->content_hash, RowHash{0}) == nullptr);
  }

  std::printf("test_recipe_integration: all tests passed\n");
  return 0;
}
