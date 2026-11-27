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

using Regions = ::fixy::FixedArray<DagRef<RegionNode>, NUM_REGIONS>;
using RegionIndex = ::fixy::Index<NUM_REGIONS>;

// Region r of the fixture: 128 ops with hashes of r, and an empty plan.
// The arena builds each op and each plan with its member initializers.
[[nodiscard]] DagRef<RegionNode> make_fixture_region(::foundation::effects::Test const& test,
                                                     ::fixy::BumpArena<DagArena>& arena, RegionIndex r) {
    return ::foundation::core::with_arena_frame(arena, [alloc = test.alloc, r](auto& frame) noexcept {
        auto ops_run = make_op_run(alloc, frame, NUM_OPS);
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TraceEntry> ops = ops_run.view();
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            TraceEntry& op = *ops.get(i).expect("i is below NUM_OPS");
            op.schema_hash = SchemaHash{0x1000 + r.raw() * 0x100 + i};
            op.shape_hash = ShapeHash{0x2000 + r.raw() * 0x100 + i};
        }

        auto const plan = frame.template make<MemoryPlan>(alloc);

        auto const region = make_region(alloc, frame, ops_run);
        frame.store(region, &RegionNode::plan, plan);
        return region.weaken();
    });
}

// The regions of the fixture, from region 0.  A braced list runs its
// elements in order, so the arena gives each region the addresses of a
// loop over r.
template <std::size_t... Region>
[[nodiscard]] Regions make_fixture_regions(::foundation::effects::Test const& test, ::fixy::BumpArena<DagArena>& arena,
                                           std::index_sequence<Region...> /*order*/) {
    return Regions{make_fixture_region(test, arena, ::fixy::index<Region>())...};
}

struct TestFixture {
    ::foundation::effects::Test test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena =
        ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 20});
    Regions regions = make_fixture_regions(test, arena, std::make_index_sequence<NUM_REGIONS>{});

    [[nodiscard]] RegionNode& region(RegionIndex r) noexcept { return *regions.at(r); }
};

// Inserts the first Count regions of the fixture, from region 0.
template <std::size_t Count>
void insert_first(RegionCache& cache, TestFixture& fix) noexcept {
    for (::fixy::Index<Count> const r : ::fixy::IndexRange<Count>{})
        cache.insert(fix.region(r));
}

// The state of a row in its home (bench::RowHome): a fixture of its own, a
// cache, and the query of the row.  The distance from the state to the frame
// of the timed loop then depends only on the code of the row.  A row fills
// the cache and the query after the build of its home.
struct CacheRow {
    TestFixture fix{};
    RegionCache cache{};
    uint32_t pos = 5;
    SchemaHash schema{};
    ShapeHash shape{};
    crucible::ConstRegionLink excl{};
    ContentHash hash{};

    // Asks for the op at pos of the region source, and excludes the region
    // excluded.
    void ask_for(RegionIndex source, RegionIndex excluded) noexcept {
        schema = fix.region(source).op_at(pos).schema_hash;
        shape = fix.region(source).op_at(pos).shape_hash;
        excl = crucible::ConstRegionLink::some(::fixy::to(fix.region(excluded)));
    }
};

// The home of a row with the step step.
template <typename Step>
[[nodiscard]] auto make_cache_row(Step step) noexcept {
    return bench::build_row_home([]() noexcept { return CacheRow{}; }, step);
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== region_cache ===\n  {} regions × {} ops, CAP={}\n\n", NUM_REGIONS, NUM_OPS, RegionCache::CAP);

    // The step of each row of find_alternate with the query of its state.
    auto const find_asked = [](CacheRow& row) noexcept {
        auto r = row.cache.find_alternate(row.pos, row.schema, row.shape, row.excl);
        bench::do_not_optimize(r);
    };
    // The step of each row of find_alternate that misses.
    auto const find_missing = [](CacheRow& row) noexcept {
        auto r = row.cache.find_alternate(5, SchemaHash{0xDEADBEEF}, ShapeHash{0xCAFEBABE});
        bench::do_not_optimize(r);
    };

    ::fixy::FixedArray<bench::Report, 10> reports{
        // ── insert ────────────────────────────────────────────────────
        // Cold: every body call starts from an empty cache. Auto-batch
        // runs the body many times per timed region; each invocation
        // re-constructs the cache so state doesn't accumulate.
        [] {
            auto home = make_cache_row([](CacheRow& row) noexcept {
                RegionCache cache;
                cache.insert(row.fix.region(::fixy::index<0>()));
                bench::do_not_optimize(cache);
            });
            return bench::run("insert: cold (empty cache)", *home);
        }(),

        // Dedup: insert the same region repeatedly; first slot hit
        // short-circuits the scan.
        [] {
            auto home = make_cache_row([](CacheRow& row) noexcept {
                row.cache.insert(row.fix.region(::fixy::index<0>()));
                bench::do_not_optimize(row.cache);
            });
            home->state.cache.insert(home->state.fix.region(::fixy::index<0>()));
            return bench::run("insert: dedup (same region)", *home);
        }(),

        // Steady-state: full cache (8 entries), insert new region →
        // full dedup scan miss + evict. Re-insert an original each
        // iteration to keep size stable across auto-batch.
        [] {
            auto home = make_cache_row([](CacheRow& row) noexcept {
                row.cache.insert(row.fix.region(::fixy::index<8>()));
                bench::do_not_optimize(row.cache);
                row.cache.insert(row.fix.region(::fixy::index<0>()));
            });
            insert_first<RegionCache::CAP>(home->state.cache, home->state.fix);
            return bench::run("insert: steady (8 entries, new)", *home);
        }(),

        // ── find_alternate ────────────────────────────────────────────
        // MRU hit (scan stops at i=0).
        [&find_asked] {
            auto home = make_cache_row(find_asked);
            insert_first<RegionCache::CAP>(home->state.cache, home->state.fix);
            home->state.ask_for(::fixy::index<7>(), ::fixy::index<6>());
            return bench::run("find_alternate: hit MRU (i=0)", *home);
        }(),

        // LRU hit (scan walks all 8 before matching).
        [&find_asked] {
            auto home = make_cache_row(find_asked);
            insert_first<RegionCache::CAP>(home->state.cache, home->state.fix);
            home->state.ask_for(::fixy::index<0>(), ::fixy::index<7>());
            return bench::run("find_alternate: hit LRU (i=7)", *home);
        }(),

        // Full miss — all 8 entries scanned, no match.
        [&find_missing] {
            auto home = make_cache_row(find_missing);
            insert_first<RegionCache::CAP>(home->state.cache, home->state.fix);
            return bench::run("find_alternate: miss (all 8 checked)", *home);
        }(),

        // 2-entry dedup-scan miss — typical for dynamic batching where
        // only a few regions are warm.
        [&find_missing] {
            auto home = make_cache_row(find_missing);
            insert_first<2>(home->state.cache, home->state.fix);
            return bench::run("find_alternate: miss (2 entries)", *home);
        }(),

        // 2-entry hit with exclude (skip MRU → match LRU).
        [&find_asked] {
            auto home = make_cache_row(find_asked);
            insert_first<2>(home->state.cache, home->state.fix);
            home->state.ask_for(::fixy::index<0>(), ::fixy::index<1>());
            return bench::run("find_alternate: hit (2 entries, excl MRU)", *home);
        }(),

        // ── find (exact content_hash) ─────────────────────────────────
        [] {
            auto home = make_cache_row([](CacheRow& row) noexcept {
                auto r = row.cache.find(row.hash);
                bench::do_not_optimize(r);
            });
            insert_first<RegionCache::CAP>(home->state.cache, home->state.fix);
            home->state.hash = home->state.fix.region(::fixy::index<7>()).content_hash;
            return bench::run("find: hit MRU", *home);
        }(),

        [] {
            auto home = make_cache_row([](CacheRow& row) noexcept {
                auto r = row.cache.find(ContentHash{0xDEADDEAD});
                bench::do_not_optimize(r);
            });
            insert_first<RegionCache::CAP>(home->state.cache, home->state.fix);
            return bench::run("find: miss (all 8 checked)", *home);
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
