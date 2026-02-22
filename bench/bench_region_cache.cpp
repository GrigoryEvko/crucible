// RegionCache micro-benchmarks — measure every hot-path operation.
//
// Targets:
//   find_alternate() hit/miss at various positions
//   find() exact lookup hit/miss
//   insert() cold start and steady-state dedup
//
// Build:  cmake --preset bench && cmake --build --preset bench -j$(nproc)
// Run:    ./build-bench/bench/bench_region_cache

#include "bench_harness.h"
#include <crucible/Effects.h>
#include <crucible/RegionCache.h>

#include <cstdio>
#include <cstring>

using namespace crucible;

// ═══════════════════════════════════════════════════════════════════
// Test fixtures: fake RegionNodes with TraceEntry arrays
//
// Each region has NUM_OPS ops, each with unique schema/shape hashes.
// Arena-allocated to match production layout (pointer chasing through
// arena memory, not stack).
// ═══════════════════════════════════════════════════════════════════

static constexpr uint32_t NUM_OPS = 128;  // realistic model op count

// We need enough regions to fill the cache (8) plus one for eviction test.
static constexpr uint32_t NUM_REGIONS = 10;

struct TestFixture {
    fx::Test test;
    Arena arena{1 << 20};  // 1MB
    RegionNode* regions[NUM_REGIONS]{};
    MemoryPlan* plans[NUM_REGIONS]{};

    TestFixture() {
        for (uint32_t r = 0; r < NUM_REGIONS; r++) {
            // Allocate TraceEntry array in arena (matches production layout).
            auto* ops = arena.alloc_array<TraceEntry>(test.alloc, NUM_OPS);
            std::memset(ops, 0, NUM_OPS * sizeof(TraceEntry));

            for (uint32_t i = 0; i < NUM_OPS; i++) {
                ops[i].schema_hash = SchemaHash{0x1000 + r * 0x100 + i};
                ops[i].shape_hash  = ShapeHash{0x2000 + r * 0x100 + i};
            }

            // Allocate a MemoryPlan (just needs to be non-null for cache eligibility).
            plans[r] = arena.alloc_obj<MemoryPlan>(test.alloc);
            std::memset(plans[r], 0, sizeof(MemoryPlan));

            // Build a RegionNode via make_region, then attach the plan.
            regions[r] = make_region(test.alloc, arena, ops, NUM_OPS);
            regions[r]->plan = plans[r];
        }
    }
};

// ═══════════════════════════════════════════════════════════════════
// Benchmarks
// ═══════════════════════════════════════════════════════════════════

static void bench_insert_cold(TestFixture& fix) {
    // Insert into empty cache — measures one insert with no dedup scan.
    BENCH_CHECK("insert: cold (empty cache)", 1'000'000, 5.4, {
        RegionCache cache;
        cache.insert(fix.regions[0]);
        bench::DoNotOptimize(cache);
    });
}

static void bench_insert_dedup(TestFixture& fix) {
    // Insert same region repeatedly — dedup check triggers on first entry.
    RegionCache cache;
    cache.insert(fix.regions[0]);

    BENCH_CHECK("insert: dedup (same region)", 1'000'000, 0.9, {
        cache.insert(fix.regions[0]);
        bench::DoNotOptimize(cache);
    });
}

static void bench_insert_steady(TestFixture& fix) {
    // Insert into full cache — dedup scan of 8 entries, then evict.
    RegionCache cache;
    for (uint32_t i = 0; i < RegionCache::CAP; i++)
        cache.insert(fix.regions[i]);

    BENCH_CHECK("insert: steady (8 entries, new)", 1'000'000, 1.7, {
        // Insert a new region (not in cache) → full dedup scan + evict.
        cache.insert(fix.regions[8]);
        bench::DoNotOptimize(cache);
        // Re-insert one of the originals to maintain 8 entries for next iter.
        cache.insert(fix.regions[0]);
    });
}

static void bench_find_alternate_hit_mru(TestFixture& fix) {
    // find_alternate hits the MRU entry (i=0 in the scan).
    RegionCache cache;
    for (uint32_t i = 0; i < RegionCache::CAP; i++)
        cache.insert(fix.regions[i]);

    // The MRU is regions[7] (last inserted).  Query its op at pos=5.
    const uint32_t pos = 5;
    auto schema = fix.regions[7]->ops[pos].schema_hash;
    auto shape  = fix.regions[7]->ops[pos].shape_hash;
    const RegionNode* exclude = fix.regions[6];  // not the MRU

    BENCH_CHECK("find_alternate: hit MRU (i=0)", 10'000'000, 1.4, {
        auto* r = cache.find_alternate(pos, schema, shape, exclude);
        bench::DoNotOptimize(r);
    });
}

static void bench_find_alternate_hit_lru(TestFixture& fix) {
    // find_alternate hits the LRU entry (i=7 in the scan, last checked).
    RegionCache cache;
    for (uint32_t i = 0; i < RegionCache::CAP; i++)
        cache.insert(fix.regions[i]);

    // The LRU is regions[0] (first inserted).  Query its op at pos=5.
    const uint32_t pos = 5;
    auto schema = fix.regions[0]->ops[pos].schema_hash;
    auto shape  = fix.regions[0]->ops[pos].shape_hash;
    const RegionNode* exclude = fix.regions[7];  // not the LRU

    BENCH_CHECK("find_alternate: hit LRU (i=7)", 10'000'000, 5.9, {
        auto* r = cache.find_alternate(pos, schema, shape, exclude);
        bench::DoNotOptimize(r);
    });
}

static void bench_find_alternate_miss(TestFixture& fix) {
    // find_alternate misses — scans all 8, no match.
    RegionCache cache;
    for (uint32_t i = 0; i < RegionCache::CAP; i++)
        cache.insert(fix.regions[i]);

    // Use hashes that don't exist in any region.
    const uint32_t pos = 5;
    auto schema = SchemaHash{0xDEADBEEF};
    auto shape  = ShapeHash{0xCAFEBABE};

    BENCH_CHECK("find_alternate: miss (all 8 checked)", 10'000'000, 6.5, {
        auto* r = cache.find_alternate(pos, schema, shape, nullptr);
        bench::DoNotOptimize(r);
    });
}

static void bench_find_alternate_miss_2entries(TestFixture& fix) {
    // find_alternate misses with only 2 entries — typical dynamic batch case.
    RegionCache cache;
    cache.insert(fix.regions[0]);
    cache.insert(fix.regions[1]);

    const uint32_t pos = 5;
    auto schema = SchemaHash{0xDEADBEEF};
    auto shape  = ShapeHash{0xCAFEBABE};

    BENCH_CHECK("find_alternate: miss (2 entries)", 10'000'000, 1.5, {
        auto* r = cache.find_alternate(pos, schema, shape, nullptr);
        bench::DoNotOptimize(r);
    });
}

static void bench_find_alternate_hit_exclude(TestFixture& fix) {
    // find_alternate with exclude — hit is second entry (i=1 after skipping MRU).
    RegionCache cache;
    cache.insert(fix.regions[0]);
    cache.insert(fix.regions[1]);

    // Query regions[0]'s hashes, exclude regions[1] (MRU).
    const uint32_t pos = 5;
    auto schema = fix.regions[0]->ops[pos].schema_hash;
    auto shape  = fix.regions[0]->ops[pos].shape_hash;
    const RegionNode* exclude = fix.regions[1];

    BENCH_CHECK("find_alternate: hit (2 entries, exclude MRU)", 10'000'000, 1.4, {
        auto* r = cache.find_alternate(pos, schema, shape, exclude);
        bench::DoNotOptimize(r);
    });
}

static void bench_find_exact_hit(TestFixture& fix) {
    // find() by content hash — hit at MRU.
    RegionCache cache;
    for (uint32_t i = 0; i < RegionCache::CAP; i++)
        cache.insert(fix.regions[i]);

    auto hash = fix.regions[7]->content_hash;

    BENCH_CHECK("find: hit MRU", 10'000'000, 0.8, {
        auto* r = cache.find(hash);
        bench::DoNotOptimize(r);
    });
}

static void bench_find_exact_miss(TestFixture& fix) {
    // find() by content hash — miss.
    RegionCache cache;
    for (uint32_t i = 0; i < RegionCache::CAP; i++)
        cache.insert(fix.regions[i]);

    BENCH_CHECK("find: miss (all 8 checked)", 10'000'000, 3.9, {
        auto* r = cache.find(ContentHash{0xDEADDEAD});
        bench::DoNotOptimize(r);
    });
}

// ═══════════════════════════════════════════════════════════════════

int main() {
    std::printf("=== RegionCache Benchmarks ===\n\n");

    TestFixture fix;

    std::printf("── insert ──\n");
    bench_insert_cold(fix);
    bench_insert_dedup(fix);
    bench_insert_steady(fix);

    std::printf("\n── find_alternate ──\n");
    bench_find_alternate_hit_mru(fix);
    bench_find_alternate_hit_lru(fix);
    bench_find_alternate_miss(fix);
    bench_find_alternate_miss_2entries(fix);
    bench_find_alternate_hit_exclude(fix);

    std::printf("\n── find (exact) ──\n");
    bench_find_exact_hit(fix);
    bench_find_exact_miss(fix);

    std::printf("\nDone.\n");
    return 0;
}
