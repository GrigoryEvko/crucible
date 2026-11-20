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
#include <utility>

#include <crucible/RegionCache.h>
#include <foundation/effects/Effect.h>

#include "bench_harness.h"

using namespace crucible;

// ── Test fixture ─────────────────────────────────────────────────────
//
// 10 RegionNodes with 128 ops each. RegionCache::CAP is 8, so 10 gives
// us one "fresh" region for eviction-steady-state tests and one spare
// for exclude() tests.

namespace {

constexpr uint32_t NUM_OPS = 128;
constexpr uint32_t NUM_REGIONS = 10;

using Regions = ::fixy::FixedArray<::fixy::Ref<RegionNode>, NUM_REGIONS>;
using RegionIndex = ::fixy::Index<NUM_REGIONS>;

// Region r of the fixture: 128 ops with hashes of r, and an empty plan.
// The arena builds each op and each plan with its member initializers.
[[nodiscard]] ::fixy::Ref<RegionNode> make_fixture_region(::foundation::effects::Test const& test,
                                                          ::fixy::BumpArena& arena, RegionIndex r) {
    const ::fixy::View<TraceEntry> ops = arena.array<TraceEntry>(test.alloc, NUM_OPS, TraceEntry{});
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below NUM_OPS");
        op.schema_hash = SchemaHash{0x1000 + r.raw() * 0x100 + i};
        op.shape_hash = ShapeHash{0x2000 + r.raw() * 0x100 + i};
    }

    auto const plan = arena.make<MemoryPlan>(test.alloc);

    ::fixy::Ref<RegionNode> const region = make_region(test.alloc, arena, ops);
    region->plan = make_plan_link(*plan);
    return region;
}

// The regions of the fixture, from region 0.  A braced list runs its
// elements in order, so the arena gives each region the addresses of a
// loop over r.
template <std::size_t... Region>
[[nodiscard]] Regions make_fixture_regions(::foundation::effects::Test const& test, ::fixy::BumpArena& arena,
                                           std::index_sequence<Region...> /*order*/) {
    return Regions{make_fixture_region(test, arena, ::fixy::index<Region>())...};
}

struct TestFixture {
    ::foundation::effects::Test test = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{1 << 20};
    Regions regions = make_fixture_regions(test, arena, std::make_index_sequence<NUM_REGIONS>{});

    [[nodiscard]] RegionNode& region(RegionIndex r) noexcept { return *regions.at(r); }
};

// Inserts the first Count regions of the fixture, from region 0.
template <std::size_t Count>
void insert_first(RegionCache& cache, TestFixture& fix) noexcept {
    for (::fixy::Index<Count> const r : ::fixy::IndexRange<Count>{})
        cache.insert(fix.region(r));
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    TestFixture fix{};
    bench::print("=== region_cache ===\n  {} regions × {} ops, CAP={}\n\n", NUM_REGIONS, NUM_OPS, RegionCache::CAP);

    ::fixy::FixedArray<bench::Report, 10> reports{
        // ── insert ────────────────────────────────────────────────────
        // Cold: every body call starts from an empty cache. Auto-batch
        // runs the body many times per timed region; each invocation
        // re-constructs the cache so state doesn't accumulate.
        bench::run("insert: cold (empty cache)",
                   [&] {
                       RegionCache cache;
                       cache.insert(fix.region(::fixy::index<0>()));
                       bench::do_not_optimize(cache);
                   }),

        // Dedup: insert the same region repeatedly; first slot hit
        // short-circuits the scan.
        [&] {
            RegionCache cache;
            cache.insert(fix.region(::fixy::index<0>()));
            return bench::run("insert: dedup (same region)", [&] {
                cache.insert(fix.region(::fixy::index<0>()));
                bench::do_not_optimize(cache);
            });
        }(),

        // Steady-state: full cache (8 entries), insert new region →
        // full dedup scan miss + evict. Re-insert an original each
        // iteration to keep size stable across auto-batch.
        [&] {
            RegionCache cache;
            insert_first<RegionCache::CAP>(cache, fix);
            return bench::run("insert: steady (8 entries, new)", [&] {
                cache.insert(fix.region(::fixy::index<8>()));
                bench::do_not_optimize(cache);
                cache.insert(fix.region(::fixy::index<0>()));
            });
        }(),

        // ── find_alternate ────────────────────────────────────────────
        // MRU hit (scan stops at i=0).
        [&] {
            RegionCache cache;
            insert_first<RegionCache::CAP>(cache, fix);
            const uint32_t pos = 5;
            const auto schema = fix.region(::fixy::index<7>()).op_at(pos).schema_hash;
            const auto shape = fix.region(::fixy::index<7>()).op_at(pos).shape_hash;
            const crucible::ConstRegionLink excl =
                crucible::ConstRegionLink::some(::fixy::erase_brand(::fixy::to(fix.region(::fixy::index<6>()))));
            return bench::run("find_alternate: hit MRU (i=0)", [&] {
                auto r = cache.find_alternate(pos, schema, shape, excl);
                bench::do_not_optimize(r);
            });
        }(),

        // LRU hit (scan walks all 8 before matching).
        [&] {
            RegionCache cache;
            insert_first<RegionCache::CAP>(cache, fix);
            const uint32_t pos = 5;
            const auto schema = fix.region(::fixy::index<0>()).op_at(pos).schema_hash;
            const auto shape = fix.region(::fixy::index<0>()).op_at(pos).shape_hash;
            const crucible::ConstRegionLink excl =
                crucible::ConstRegionLink::some(::fixy::erase_brand(::fixy::to(fix.region(::fixy::index<7>()))));
            return bench::run("find_alternate: hit LRU (i=7)", [&] {
                auto r = cache.find_alternate(pos, schema, shape, excl);
                bench::do_not_optimize(r);
            });
        }(),

        // Full miss — all 8 entries scanned, no match.
        [&] {
            RegionCache cache;
            insert_first<RegionCache::CAP>(cache, fix);
            return bench::run("find_alternate: miss (all 8 checked)", [&] {
                auto r = cache.find_alternate(5, SchemaHash{0xDEADBEEF}, ShapeHash{0xCAFEBABE});
                bench::do_not_optimize(r);
            });
        }(),

        // 2-entry dedup-scan miss — typical for dynamic batching where
        // only a few regions are warm.
        [&] {
            RegionCache cache;
            cache.insert(fix.region(::fixy::index<0>()));
            cache.insert(fix.region(::fixy::index<1>()));
            return bench::run("find_alternate: miss (2 entries)", [&] {
                auto r = cache.find_alternate(5, SchemaHash{0xDEADBEEF}, ShapeHash{0xCAFEBABE});
                bench::do_not_optimize(r);
            });
        }(),

        // 2-entry hit with exclude (skip MRU → match LRU).
        [&] {
            RegionCache cache;
            cache.insert(fix.region(::fixy::index<0>()));
            cache.insert(fix.region(::fixy::index<1>()));
            const uint32_t pos = 5;
            const auto schema = fix.region(::fixy::index<0>()).op_at(pos).schema_hash;
            const auto shape = fix.region(::fixy::index<0>()).op_at(pos).shape_hash;
            const crucible::ConstRegionLink excl =
                crucible::ConstRegionLink::some(::fixy::erase_brand(::fixy::to(fix.region(::fixy::index<1>()))));
            return bench::run("find_alternate: hit (2 entries, excl MRU)", [&] {
                auto r = cache.find_alternate(pos, schema, shape, excl);
                bench::do_not_optimize(r);
            });
        }(),

        // ── find (exact content_hash) ─────────────────────────────────
        [&] {
            RegionCache cache;
            insert_first<RegionCache::CAP>(cache, fix);
            const auto hash = fix.region(::fixy::index<7>()).content_hash;
            return bench::run("find: hit MRU", [&] {
                auto r = cache.find(hash);
                bench::do_not_optimize(r);
            });
        }(),

        [&] {
            RegionCache cache;
            insert_first<RegionCache::CAP>(cache, fix);
            return bench::run("find: miss (all 8 checked)", [&] {
                auto r = cache.find(ContentHash{0xDEADDEAD});
                bench::do_not_optimize(r);
            });
        }(),
    };

    bench::emit_reports_text(reports.view());

    // Scan-depth compare: 2-entry vs 8-entry miss. 2-entry should be
    // ~4× faster if the scan is linear.
    bench::print("\n=== compare — scan depth ===\n  ");
    bench::compare(reports.at(::fixy::index<6>()), reports.at(::fixy::index<5>())).print_text();

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
