// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0

#include "test_kernels.h"

#include <crucible/KernelCache.h>
#include <crucible/MerkleDag.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <fixy/check/Check.h>
#include <cstdio>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using crucible::ContentHash;
using crucible::KernelCache;
using crucible::NumericalRecipe;
using crucible::RecipeHash;
using crucible::RecipePool;
using crucible::RecipeRegistry;
using crucible::SchemaHash;
using crucible::TraceEntry;
using crucible::DagArena;
using crucible::RecipeArena;
using crucible::test::is_found;
using crucible::test::make_kernel;
using fixy::BumpArena;

namespace names = crucible::recipe_names;

// The block size of each arena of the test.
constexpr ::fixy::Quantity<::fixy::unit::Byte, size_t> kArenaBlock{size_t{1} << 20};

auto g_test = ::foundation::effects::testing::test();
auto g_init = ::foundation::effects::testing::init();
inline ::foundation::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }
inline ::foundation::effects::Init init_cap() noexcept { return g_init; }

// A context whose row is empty, which each read of the registry admits.
constexpr ::fixy::HotFgCtx kPure = ::foundation::effects::testing::foreground();

// The schema hashes are fixed so a comparison across pools or processes
// stays stable.  The ops are a run of an arena, because the region keeps
// them.
constexpr uint32_t kOpsCount = 3;
inline void mk_ops(::fixy::View<TraceEntry> run) noexcept {
    ::fixy::View<TraceEntry, kOpsCount> const buf = run.window<kOpsCount>(0).expect("the run holds kOpsCount ops");
    buf.at(::fixy::index<0>()).schema_hash = SchemaHash{0xAA01};
    buf.at(::fixy::index<1>()).schema_hash = SchemaHash{0xBB02};
    buf.at(::fixy::index<2>()).schema_hash = SchemaHash{0xCC03};
}

}  // namespace

[[gnu::cold]] int main() {
    // Assuming that one recipe name yields one pointer everywhere breaks
    // as soon as a second process starts.  Pointer identity holds within a
    // pool, and identity across pools runs through the recipe hash.
    {
        BumpArena<RecipeArena> arena_a = ::fixy::mint_bump_arena<RecipeArena>(kArenaBlock);
        BumpArena<RecipeArena> arena_b = ::fixy::mint_bump_arena<RecipeArena>(kArenaBlock);
        RecipePool pool_a{::fixy::to(arena_a), init_cap()};
        RecipePool pool_b{::fixy::to(arena_b), init_cap()};
        RecipeRegistry reg_a{::fixy::to(pool_a), alloc_cap()};
        RecipeRegistry reg_b{::fixy::to(pool_b), alloc_cap()};

        RecipeRegistry::RecipeRef const a =
            reg_a.by_name(kPure, names::kF16F32AccumTc).expect("the starter is present");
        RecipeRegistry::RecipeRef const b =
            reg_b.by_name(kPure, names::kF16F32AccumTc).expect("the starter is present");

        // Two pools, two addresses.
        CRUCIBLE_TEST_ASSERT(kTest, !::fixy::same_object(a, b));
        // One name, one hash, whichever pool interned it.
        CRUCIBLE_TEST_ASSERT(kTest, a->hash == b->hash);
        CRUCIBLE_TEST_ASSERT(kTest, a->accum_dtype == b->accum_dtype);
        CRUCIBLE_TEST_ASSERT(kTest, a->out_dtype == b->out_dtype);
        CRUCIBLE_TEST_ASSERT(kTest, a->determinism == b->determinism);
    }

    // Two regions with byte-identical ops but different recipes must hash
    // differently.  Otherwise a kernel compiled under one recipe is served
    // on a lookup pinned to the other, which produces wrong bits and
    // breaks replay determinism.
    {
        BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(kArenaBlock);
        BumpArena<RecipeArena> recipe_arena = ::fixy::mint_bump_arena<RecipeArena>(kArenaBlock);
        RecipePool pool{::fixy::to(recipe_arena), init_cap()};
        RecipeRegistry registry{::fixy::to(pool), alloc_cap()};

        RecipeRegistry::RecipeRef const rec_tc =
            registry.by_name(kPure, names::kF16F32AccumTc).expect("the starter is present");
        RecipeRegistry::RecipeRef const rec_strict =
            registry.by_name(kPure, names::kF32Strict).expect("the starter is present");

        auto ops_run = arena.array(alloc_cap(), kOpsCount, TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        ::fixy::View<TraceEntry> const ops = ops_run.view();
        mk_ops(ops);

        const crucible::RegionNode& region_tc = *crucible::make_region(alloc_cap(), arena, ops_run, *rec_tc);
        const crucible::RegionNode& region_strict = *crucible::make_region(alloc_cap(), arena, ops_run, *rec_strict);
        // The overload with no recipe.
        const crucible::RegionNode& region_none = *crucible::make_region(alloc_cap(), arena, ops_run);

        // Three recipe pins, counting the unpinned baseline, give three
        // hashes.
        CRUCIBLE_TEST_ASSERT(kTest, region_tc.content_hash != region_strict.content_hash);
        CRUCIBLE_TEST_ASSERT(kTest, region_tc.content_hash != region_none.content_hash);
        CRUCIBLE_TEST_ASSERT(kTest, region_strict.content_hash != region_none.content_hash);

        // A hash of zero would mean the mix collapsed.
        CRUCIBLE_TEST_ASSERT(kTest, region_tc.content_hash.raw() != 0);
        CRUCIBLE_TEST_ASSERT(kTest, region_strict.content_hash.raw() != 0);
        CRUCIBLE_TEST_ASSERT(kTest, region_none.content_hash.raw() != 0);
    }

    // A recovery process loads a persisted blob and must recover the
    // recipe identity from the persisted hash.  Otherwise the recovered
    // weights are silently mistyped against whichever recipe the loader
    // assumed.  Recovery runs by hash, which returns the canonical
    // interned pointer in the current process.
    {
        BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(kArenaBlock);
        BumpArena<RecipeArena> recipe_arena = ::fixy::mint_bump_arena<RecipeArena>(kArenaBlock);
        RecipePool pool{::fixy::to(recipe_arena), init_cap()};
        RecipeRegistry registry{::fixy::to(pool), alloc_cap()};

        // Persistence time: the original process pins a recipe and takes
        // the region's content hash.
        const NumericalRecipe* recipe_orig =
            &*registry.by_name(kPure, names::kBf16F32AccumTc).expect("the starter is present");

        auto ops_run = arena.array(alloc_cap(), kOpsCount, TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        ::fixy::View<TraceEntry> const ops = ops_run.view();
        mk_ops(ops);
        const crucible::RegionNode& region_orig = *crucible::make_region(alloc_cap(), arena, ops_run, *recipe_orig);
        const ContentHash persisted_content_hash = region_orig.content_hash;

        // What is persisted per region is the pair of the region's content
        // hash and the recipe's hash.  Both are eight-byte values and both
        // are byte-stable across processes.
        const RecipeHash persisted_recipe_hash = recipe_orig->hash;

        // Recovery time.  This runs in the same process, but the call path
        // is the one a fresh recovery process takes.
        RecipeRegistry::RecipeRef const recovered =
            registry.by_hash(kPure, persisted_recipe_hash).expect("the persisted hash resolves");
        // Recovery inside one process lands on the canonical pointer.
        CRUCIBLE_TEST_ASSERT(kTest, &*recovered == recipe_orig);
        CRUCIBLE_TEST_ASSERT(kTest, recovered->hash == persisted_recipe_hash);

        // Rebuilding the region with the recovered recipe reproduces the
        // original content hash, which is the end-to-end claim.
        const crucible::RegionNode& region_recovered = *crucible::make_region(alloc_cap(), arena, ops_run, *recovered);
        CRUCIBLE_TEST_ASSERT(kTest, region_recovered.content_hash == persisted_content_hash);
    }

    // A fresh process must not produce a different content hash for the
    // same region.  Its pointers differ, but the content hash is mixed
    // from the recipe hash rather than the pointer, so it comes out the
    // same.  Distributed checkpoint recovery and federated cache sharing
    // both rest on that.
    {
        // The original process.
        BumpArena<DagArena> arena_a = ::fixy::mint_bump_arena<DagArena>(kArenaBlock);
        BumpArena<RecipeArena> recipe_arena_a = ::fixy::mint_bump_arena<RecipeArena>(kArenaBlock);
        RecipePool pool_a{::fixy::to(recipe_arena_a), init_cap()};
        RecipeRegistry reg_a{::fixy::to(pool_a), alloc_cap()};
        RecipeRegistry::RecipeRef const rec_a =
            reg_a.by_name(kPure, names::kFp8E4m3F32AccumMxOrd).expect("the starter is present");

        auto ops_a_run = arena_a.array(alloc_cap(), kOpsCount, TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        ::fixy::View<TraceEntry> const ops_a = ops_a_run.view();
        mk_ops(ops_a);
        const crucible::RegionNode& region_a = *crucible::make_region(alloc_cap(), arena_a, ops_a_run, *rec_a);
        const ContentHash hash_in_proc_a = region_a.content_hash;

        // A second process, with its own pool, registry and arena, and so
        // with different addresses throughout.
        BumpArena<DagArena> arena_b = ::fixy::mint_bump_arena<DagArena>(kArenaBlock);
        BumpArena<RecipeArena> recipe_arena_b = ::fixy::mint_bump_arena<RecipeArena>(kArenaBlock);
        RecipePool pool_b{::fixy::to(recipe_arena_b), init_cap()};
        RecipeRegistry reg_b{::fixy::to(pool_b), alloc_cap()};
        RecipeRegistry::RecipeRef const rec_b =
            reg_b.by_name(kPure, names::kFp8E4m3F32AccumMxOrd).expect("the starter is present");
        // Different pointers, so the two pools really are independent.
        CRUCIBLE_TEST_ASSERT(kTest, !::fixy::same_object(rec_a, rec_b));
        // One hash, which is the identity that crosses them.
        CRUCIBLE_TEST_ASSERT(kTest, rec_a->hash == rec_b->hash);

        auto ops_b_run = arena_b.array(alloc_cap(), kOpsCount, TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        ::fixy::View<TraceEntry> const ops_b = ops_b_run.view();
        mk_ops(ops_b);
        const crucible::RegionNode& region_b = *crucible::make_region(alloc_cap(), arena_b, ops_b_run, *rec_b);
        const ContentHash hash_in_proc_b = region_b.content_hash;

        // The content hashes agree although no pointer is shared, which is
        // what makes a persisted hash a valid cache key in any process.
        CRUCIBLE_TEST_ASSERT(kTest, hash_in_proc_a == hash_in_proc_b);
    }

    // A cache filled while training under one recipe would hand those
    // kernels back after a restart under another one, with wrong numerics
    // and no diagnostic.  The content hash is recipe-aware, so the cache
    // key is too, and the two recipes cannot share a slot.
    {
        BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(kArenaBlock);
        BumpArena<RecipeArena> recipe_arena = ::fixy::mint_bump_arena<RecipeArena>(kArenaBlock);
        RecipePool pool{::fixy::to(recipe_arena), init_cap()};
        RecipeRegistry registry{::fixy::to(pool), alloc_cap()};

        RecipeRegistry::RecipeRef const rec_tc =
            registry.by_name(kPure, names::kF16F32AccumTc).expect("the starter is present");
        RecipeRegistry::RecipeRef const rec_ord =
            registry.by_name(kPure, names::kF16F32AccumOrdered).expect("the starter is present");
        RecipeRegistry::RecipeRef const rec_bf16_tc =
            registry.by_name(kPure, names::kBf16F32AccumTc).expect("the starter is present");

        auto ops_run = arena.array(alloc_cap(), kOpsCount, TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        ::fixy::View<TraceEntry> const ops = ops_run.view();
        mk_ops(ops);

        const crucible::RegionNode& region_tc = *crucible::make_region(alloc_cap(), arena, ops_run, *rec_tc);
        const crucible::RegionNode& region_ord = *crucible::make_region(alloc_cap(), arena, ops_run, *rec_ord);
        const crucible::RegionNode& region_bf16_tc = *crucible::make_region(alloc_cap(), arena, ops_run, *rec_bf16_tc);

        // Three hashes, so three cache slots.
        CRUCIBLE_TEST_ASSERT(kTest, region_tc.content_hash != region_ord.content_hash);
        CRUCIBLE_TEST_ASSERT(kTest, region_tc.content_hash != region_bf16_tc.content_hash);
        CRUCIBLE_TEST_ASSERT(kTest, region_ord.content_hash != region_bf16_tc.content_hash);

        crucible::test::Kernels kernels = crucible::test::make_kernel_arena();
        KernelCache cache{alloc_cap()};
        using crucible::RowHash;
        // A cache key pairs a content hash with a row hash.  These cases
        // vary only the content hash, so every call passes the bare-type
        // row hash of zero.  The two axes partition the cache independently.

        // Training under the first recipe inserts one kernel.
        auto const kernel_tc = make_kernel(kernels);
        auto ins = cache.insert(g_test.nondet, region_tc.content_hash, RowHash{0}, kernel_tc);
        CRUCIBLE_TEST_ASSERT(kTest, ins.is_ok());
        CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(g_test.nondet, region_tc.content_hash, RowHash{0}), kernel_tc));

        // A lookup under the second recipe must miss.  Were the two sharing
        // a slot, the first recipe's kernel would serve the second recipe's
        // work.
        CRUCIBLE_FATAL_INVARIANT(cache.lookup(g_test.nondet, region_ord.content_hash, RowHash{0}).is_none());

        // The third recipe is isolated the same way.
        CRUCIBLE_FATAL_INVARIANT(cache.lookup(g_test.nondet, region_bf16_tc.content_hash, RowHash{0}).is_none());

        // The first recipe still finds its own kernel.
        CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(g_test.nondet, region_tc.content_hash, RowHash{0}), kernel_tc));

        // Two kernels under two recipes coexist, each reachable only
        // through its own key.
        auto const kernel_ord = make_kernel(kernels);
        auto ins2 = cache.insert(g_test.nondet, region_ord.content_hash, RowHash{0}, kernel_ord);
        CRUCIBLE_TEST_ASSERT(kTest, ins2.is_ok());
        CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(g_test.nondet, region_tc.content_hash, RowHash{0}), kernel_tc));
        CRUCIBLE_FATAL_INVARIANT(
            is_found(cache.lookup(g_test.nondet, region_ord.content_hash, RowHash{0}), kernel_ord));
        CRUCIBLE_FATAL_INVARIANT(cache.lookup(g_test.nondet, region_bf16_tc.content_hash, RowHash{0}).is_none());
    }

    // The search run of prop_content_hash_recipe_disambiguates with the seed
    // 0x3934336a14cbd76a drew the recipe whose eight semantic bytes are zero,
    // at the iteration 25152948.  The pool interns it, and a region hashes
    // under it.  The content hash refuses a recipe hash of 0, so the hash of
    // the interned recipe must not be 0.
    {
        BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(kArenaBlock);
        BumpArena<RecipeArena> recipe_arena = ::fixy::mint_bump_arena<RecipeArena>(kArenaBlock);
        RecipePool pool{::fixy::to(recipe_arena), init_cap()};
        const NumericalRecipe zero_bytes{
            .accum_dtype = crucible::ScalarType::Byte,
            .out_dtype = crucible::ScalarType::Byte,
            .reduction_algo = crucible::ReductionAlgo::PAIRWISE,
            .rounding = ::foundation::core::RoundingMode::RN,
            .scale_policy = crucible::ScalePolicy::NONE,
            .softmax = crucible::SoftmaxRecurrence::NAIVE,
            .determinism = crucible::ReductionDeterminism::UNORDERED,
            .flags = {},
            .hash = {},
        };
        auto entry_run = arena.array(alloc_cap(), 1, TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        ::fixy::View<TraceEntry> const entry = entry_run.view();
        entry.get(0).expect("the run holds one op")->schema_hash = SchemaHash{0xAA01};

        const ContentHash with_recipe =
            crucible::make_region(alloc_cap(), arena, entry_run, *pool.intern(alloc_cap(), zero_bytes))->content_hash;
        const ContentHash with_no_recipe = crucible::make_region(alloc_cap(), arena, entry_run)->content_hash;
        CRUCIBLE_TEST_ASSERT(kTest, with_recipe != with_no_recipe);
        CRUCIBLE_TEST_ASSERT(kTest, with_recipe.raw() != 0);
    }

    ::fixy::check::pass(kTest, "test_recipe_integration: all tests passed\n");
    return 0;
}
