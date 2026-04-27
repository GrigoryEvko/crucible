// RegionCache micro-benchmarks — every hot-path op individually.
//
// Targets:
//   find_alternate() hit at MRU (i=0) / LRU (i=7) / miss (all 8 checked)
//   find() exact lookup hit / miss
//   insert() cold / dedup / steady-state
//
// Uses a shared TestFixture of 10 RegionNodes so scenarios can compare
// against each other (e.g., a dedup miss scan from a 2-entry cache vs
// a full 8-entry cache should be ~4× faster).

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/RegionCache.h>

#include "bench_harness.h"

using namespace crucible;

// ── Test fixture ─────────────────────────────────────────────────────
//
// 10 RegionNodes with 128 ops each. RegionCache::CAP is 8, so 10 gives
// us one "fresh" region for eviction-steady-state tests and one spare
// for exclude() tests.

namespace {

constexpr uint32_t NUM_OPS     = 128;
constexpr uint32_t NUM_REGIONS = 10;

struct TestFixture {
    effects::Test    test{};
    Arena       arena{1 << 20};
    RegionNode* regions[NUM_REGIONS]{};
    MemoryPlan* plans[NUM_REGIONS]{};

    TestFixture() {
        for (uint32_t r = 0; r < NUM_REGIONS; r++) {
            auto* ops = arena.alloc_array<TraceEntry>(test.alloc, NUM_OPS);
            std::memset(ops, 0, NUM_OPS * sizeof(TraceEntry));
            for (uint32_t i = 0; i < NUM_OPS; i++) {
                ops[i].schema_hash = SchemaHash{0x1000 + r * 0x100 + i};
                ops[i].shape_hash  = ShapeHash{0x2000 + r * 0x100 + i};
            }

            plans[r] = arena.alloc_obj<MemoryPlan>(test.alloc);
            std::memset(plans[r], 0, sizeof(MemoryPlan));

            regions[r] = make_region(test.alloc, arena, ops, NUM_OPS);
            regions[r]->plan = plans[r];
        }
    }
};

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    TestFixture fix{};
    std::printf("=== region_cache ===\n  %u regions × %u ops, CAP=%u\n\n",
                NUM_REGIONS, NUM_OPS, RegionCache::CAP);

    bench::Report reports[] = {
        // ── insert ────────────────────────────────────────────────────
        // Cold: every body call starts from an empty cache. Auto-batch
        // runs the body many times per timed region; each invocation
        // re-constructs the cache so state doesn't accumulate.
        bench::run("insert: cold (empty cache)", [&]{
            RegionCache cache;
            cache.insert(fix.regions[0]);
            bench::do_not_optimize(cache);
        }),

        // Dedup: insert the same region repeatedly; first slot hit
        // short-circuits the scan.
        [&]{
            RegionCache cache;
            cache.insert(fix.regions[0]);
            return bench::run("insert: dedup (same region)", [&]{
                cache.insert(fix.regions[0]);
                bench::do_not_optimize(cache);
            });
        }(),

        // Steady-state: full cache (8 entries), insert new region →
        // full dedup scan miss + evict. Re-insert an original each
        // iteration to keep size stable across auto-batch.
        [&]{
            RegionCache cache;
            for (uint32_t i = 0; i < RegionCache::CAP; i++)
                cache.insert(fix.regions[i]);
            return bench::run("insert: steady (8 entries, new)", [&]{
                cache.insert(fix.regions[8]);
                bench::do_not_optimize(cache);
                cache.insert(fix.regions[0]);
            });
        }(),

        // ── find_alternate ────────────────────────────────────────────
        // MRU hit (scan stops at i=0).
        [&]{
            RegionCache cache;
            for (uint32_t i = 0; i < RegionCache::CAP; i++)
                cache.insert(fix.regions[i]);
            const uint32_t pos    = 5;
            const auto schema     = fix.regions[7]->ops[pos].schema_hash;
            const auto shape      = fix.regions[7]->ops[pos].shape_hash;
            const RegionNode* excl = fix.regions[6];
            return bench::run("find_alternate: hit MRU (i=0)", [&]{
                auto* r = cache.find_alternate(pos, schema, shape, excl);
                bench::do_not_optimize(r);
            });
        }(),

        // LRU hit (scan walks all 8 before matching).
        [&]{
            RegionCache cache;
            for (uint32_t i = 0; i < RegionCache::CAP; i++)
                cache.insert(fix.regions[i]);
            const uint32_t pos    = 5;
            const auto schema     = fix.regions[0]->ops[pos].schema_hash;
            const auto shape      = fix.regions[0]->ops[pos].shape_hash;
            const RegionNode* excl = fix.regions[7];
            return bench::run("find_alternate: hit LRU (i=7)", [&]{
                auto* r = cache.find_alternate(pos, schema, shape, excl);
                bench::do_not_optimize(r);
            });
        }(),

        // Full miss — all 8 entries scanned, no match.
        [&]{
            RegionCache cache;
            for (uint32_t i = 0; i < RegionCache::CAP; i++)
                cache.insert(fix.regions[i]);
            return bench::run("find_alternate: miss (all 8 checked)", [&]{
                auto* r = cache.find_alternate(
                    5, SchemaHash{0xDEADBEEF}, ShapeHash{0xCAFEBABE}, nullptr);
                bench::do_not_optimize(r);
            });
        }(),

        // 2-entry dedup-scan miss — typical for dynamic batching where
        // only a few regions are warm.
        [&]{
            RegionCache cache;
            cache.insert(fix.regions[0]);
            cache.insert(fix.regions[1]);
            return bench::run("find_alternate: miss (2 entries)", [&]{
                auto* r = cache.find_alternate(
                    5, SchemaHash{0xDEADBEEF}, ShapeHash{0xCAFEBABE}, nullptr);
                bench::do_not_optimize(r);
            });
        }(),

        // 2-entry hit with exclude (skip MRU → match LRU).
        [&]{
            RegionCache cache;
            cache.insert(fix.regions[0]);
            cache.insert(fix.regions[1]);
            const uint32_t pos = 5;
            const auto schema  = fix.regions[0]->ops[pos].schema_hash;
            const auto shape   = fix.regions[0]->ops[pos].shape_hash;
            const RegionNode* excl = fix.regions[1];
            return bench::run("find_alternate: hit (2 entries, excl MRU)", [&]{
                auto* r = cache.find_alternate(pos, schema, shape, excl);
                bench::do_not_optimize(r);
            });
        }(),

        // ── find (exact content_hash) ─────────────────────────────────
        [&]{
            RegionCache cache;
            for (uint32_t i = 0; i < RegionCache::CAP; i++)
                cache.insert(fix.regions[i]);
            const auto hash = fix.regions[7]->content_hash;
            return bench::run("find: hit MRU", [&]{
                auto* r = cache.find(hash);
                bench::do_not_optimize(r);
            });
        }(),

        [&]{
            RegionCache cache;
            for (uint32_t i = 0; i < RegionCache::CAP; i++)
                cache.insert(fix.regions[i]);
            return bench::run("find: miss (all 8 checked)", [&]{
                auto* r = cache.find(ContentHash{0xDEADDEAD});
                bench::do_not_optimize(r);
            });
        }(),
    };

    bench::emit_reports_text(reports);

    // Scan-depth compare: 2-entry vs 8-entry miss. 2-entry should be
    // ~4× faster if the scan is linear.
    std::printf("\n=== compare — scan depth ===\n  ");
    bench::compare(reports[6], reports[5]).print_text(stdout);

    bench::emit_reports_json(reports, json);
    return 0;
}
