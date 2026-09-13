// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0

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

auto g_test = crucible::effects::testing::test();
auto g_init = crucible::effects::testing::init();
inline crucible::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }
inline crucible::effects::Init init_cap() noexcept { return g_init; }

[[nodiscard]] CompiledKernel* kernel_ptr(void* p) noexcept { return static_cast<CompiledKernel*>(p); }

// The schema hashes are fixed so a comparison across pools or processes
// stays stable.
constexpr uint32_t kOpsCount = 3;
inline void mk_ops(TraceEntry (&buf)[kOpsCount]) noexcept {
    buf[0].schema_hash = SchemaHash{0xAA01};
    buf[1].schema_hash = SchemaHash{0xBB02};
    buf[2].schema_hash = SchemaHash{0xCC03};
}

}  // namespace

[[gnu::cold]] int main() {
    // Assuming that one recipe name yields one pointer everywhere breaks
    // as soon as a second process starts.  Pointer identity holds within a
    // pool, and identity across pools runs through the recipe hash.
    {
        Arena arena_a{};
        Arena arena_b{};
        RecipePool pool_a{RecipePool::ArenaBorrow{arena_a}, init_cap()};
        RecipePool pool_b{RecipePool::ArenaBorrow{arena_b}, init_cap()};
        RecipeRegistry reg_a{RecipeRegistry::PoolBorrow{pool_a}, alloc_cap()};
        RecipeRegistry reg_b{RecipeRegistry::PoolBorrow{pool_b}, alloc_cap()};

        auto a = reg_a.by_name(names::kF16F32AccumTc);
        auto b = reg_b.by_name(names::kF16F32AccumTc);
        assert(a.has_value() && b.has_value());

        // Two pools, two addresses.
        assert(*a != *b);
        // One name, one hash, whichever pool interned it.
        assert((*a)->hash == (*b)->hash);
        assert((*a)->accum_dtype == (*b)->accum_dtype);
        assert((*a)->out_dtype == (*b)->out_dtype);
        assert((*a)->determinism == (*b)->determinism);
    }

    // Two regions with byte-identical ops but different recipes must hash
    // differently.  Otherwise a kernel compiled under one recipe is served
    // on a lookup pinned to the other, which produces wrong bits and
    // breaks replay determinism.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry registry{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        auto rec_tc = registry.by_name(names::kF16F32AccumTc);
        auto rec_strict = registry.by_name(names::kF32Strict);
        assert(rec_tc.has_value() && rec_strict.has_value());

        TraceEntry ops[kOpsCount]{};
        mk_ops(ops);

        auto* region_tc = crucible::make_region(alloc_cap(), arena, ops, kOpsCount, *rec_tc);
        auto* region_strict = crucible::make_region(alloc_cap(), arena, ops, kOpsCount, *rec_strict);
        auto* region_none = crucible::make_region(alloc_cap(), arena, ops, kOpsCount);  // no-recipe overload

        // Three recipe pins, counting the unpinned baseline, give three
        // hashes.
        assert(region_tc->content_hash != region_strict->content_hash);
        assert(region_tc->content_hash != region_none->content_hash);
        assert(region_strict->content_hash != region_none->content_hash);

        // A hash of zero would mean the mix collapsed.
        assert(region_tc->content_hash.raw() != 0);
        assert(region_strict->content_hash.raw() != 0);
        assert(region_none->content_hash.raw() != 0);
    }

    // A recovery process loads a persisted blob and must recover the
    // recipe identity from the persisted hash.  Otherwise the recovered
    // weights are silently mistyped against whichever recipe the loader
    // assumed.  Recovery runs by hash, which returns the canonical
    // interned pointer in the current process.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry registry{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        // Persistence time: the original process pins a recipe and takes
        // the region's content hash.
        auto pin = registry.by_name(names::kBf16F32AccumTc);
        assert(pin.has_value());
        const NumericalRecipe* recipe_orig = *pin;

        TraceEntry ops[kOpsCount]{};
        mk_ops(ops);
        auto* region_orig = crucible::make_region(alloc_cap(), arena, ops, kOpsCount, recipe_orig);
        const ContentHash persisted_content_hash = region_orig->content_hash;

        // What is persisted per region is the pair of the region's content
        // hash and the recipe's hash.  Both are eight-byte values and both
        // are byte-stable across processes.
        const RecipeHash persisted_recipe_hash = recipe_orig->hash;

        // Recovery time.  This runs in the same process, but the call path
        // is the one a fresh recovery process takes.
        auto recovered = registry.by_hash(persisted_recipe_hash);
        assert(recovered.has_value());
        // Recovery inside one process lands on the canonical pointer.
        assert(*recovered == recipe_orig);
        assert((*recovered)->hash == persisted_recipe_hash);

        // Rebuilding the region with the recovered recipe reproduces the
        // original content hash, which is the end-to-end claim.
        auto* region_recovered = crucible::make_region(alloc_cap(), arena, ops, kOpsCount, *recovered);
        assert(region_recovered->content_hash == persisted_content_hash);
    }

    // A fresh process must not produce a different content hash for the
    // same region.  Its pointers differ, but the content hash is mixed
    // from the recipe hash rather than the pointer, so it comes out the
    // same.  Distributed checkpoint recovery and federated cache sharing
    // both rest on that.
    {
        // The original process.
        Arena arena_a{};
        RecipePool pool_a{RecipePool::ArenaBorrow{arena_a}, init_cap()};
        RecipeRegistry reg_a{RecipeRegistry::PoolBorrow{pool_a}, alloc_cap()};
        auto rec_a = reg_a.by_name(names::kFp8E4m3F32AccumMxOrd);
        assert(rec_a.has_value());

        TraceEntry ops_a[kOpsCount]{};
        mk_ops(ops_a);
        auto* region_a = crucible::make_region(alloc_cap(), arena_a, ops_a, kOpsCount, *rec_a);
        const ContentHash hash_in_proc_a = region_a->content_hash;

        // A second process, with its own pool, registry and arena, and so
        // with different addresses throughout.
        Arena arena_b{};
        RecipePool pool_b{RecipePool::ArenaBorrow{arena_b}, init_cap()};
        RecipeRegistry reg_b{RecipeRegistry::PoolBorrow{pool_b}, alloc_cap()};
        auto rec_b = reg_b.by_name(names::kFp8E4m3F32AccumMxOrd);
        assert(rec_b.has_value());
        // Different pointers, so the two pools really are independent.
        assert(*rec_a != *rec_b);
        // One hash, which is the identity that crosses them.
        assert((*rec_a)->hash == (*rec_b)->hash);

        TraceEntry ops_b[kOpsCount]{};
        mk_ops(ops_b);
        auto* region_b = crucible::make_region(alloc_cap(), arena_b, ops_b, kOpsCount, *rec_b);
        const ContentHash hash_in_proc_b = region_b->content_hash;

        // The content hashes agree although no pointer is shared, which is
        // what makes a persisted hash a valid cache key in any process.
        assert(hash_in_proc_a == hash_in_proc_b);
    }

    // A cache filled while training under one recipe would hand those
    // kernels back after a restart under another one, with wrong numerics
    // and no diagnostic.  The content hash is recipe-aware, so the cache
    // key is too, and the two recipes cannot share a slot.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry registry{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        auto rec_tc = registry.by_name(names::kF16F32AccumTc);
        auto rec_ord = registry.by_name(names::kF16F32AccumOrdered);
        auto rec_bf16_tc = registry.by_name(names::kBf16F32AccumTc);
        assert(rec_tc.has_value() && rec_ord.has_value() && rec_bf16_tc.has_value());

        TraceEntry ops[kOpsCount]{};
        mk_ops(ops);

        auto* region_tc = crucible::make_region(alloc_cap(), arena, ops, kOpsCount, *rec_tc);
        auto* region_ord = crucible::make_region(alloc_cap(), arena, ops, kOpsCount, *rec_ord);
        auto* region_bf16_tc = crucible::make_region(alloc_cap(), arena, ops, kOpsCount, *rec_bf16_tc);

        // Three hashes, so three cache slots.
        assert(region_tc->content_hash != region_ord->content_hash);
        assert(region_tc->content_hash != region_bf16_tc->content_hash);
        assert(region_ord->content_hash != region_bf16_tc->content_hash);

        KernelCache cache;
        using crucible::RowHash;
        // A cache key pairs a content hash with a row hash.  These cases
        // vary only the content hash, so every call passes the bare-type
        // row hash of zero.  The two axes partition the cache independently.

        // Training under the first recipe inserts one kernel.
        struct FakeKernel {
            const char* tag;
        };
        FakeKernel kernel_tc{"f16_tc-compiled"};
        auto ins = cache.insert(region_tc->content_hash, RowHash{0}, kernel_ptr(&kernel_tc));
        assert(ins.has_value());
        assert(cache.lookup(region_tc->content_hash, RowHash{0}) == kernel_ptr(&kernel_tc));

        // A lookup under the second recipe must miss.  Were the two sharing
        // a slot, the first recipe's kernel would serve the second recipe's
        // work.
        assert(cache.lookup(region_ord->content_hash, RowHash{0}) == nullptr);

        // The third recipe is isolated the same way.
        assert(cache.lookup(region_bf16_tc->content_hash, RowHash{0}) == nullptr);

        // The first recipe still finds its own kernel.
        assert(cache.lookup(region_tc->content_hash, RowHash{0}) == kernel_ptr(&kernel_tc));

        // Two kernels under two recipes coexist, each reachable only
        // through its own key.
        FakeKernel kernel_ord{"f16_ordered-compiled"};
        auto ins2 = cache.insert(region_ord->content_hash, RowHash{0}, kernel_ptr(&kernel_ord));
        assert(ins2.has_value());
        assert(cache.lookup(region_tc->content_hash, RowHash{0}) == kernel_ptr(&kernel_tc));
        assert(cache.lookup(region_ord->content_hash, RowHash{0}) == kernel_ptr(&kernel_ord));
        assert(cache.lookup(region_bf16_tc->content_hash, RowHash{0}) == nullptr);
    }

    std::printf("test_recipe_integration: all tests passed\n");
    return 0;
}
