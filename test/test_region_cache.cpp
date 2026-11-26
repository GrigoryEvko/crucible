// The region cache, alone and behind a Vigil that switches between two
// cached variants of one op stream.
//
// The test is several source files of one executable, so that no
// translation unit holds every test:
//
//   region_cache.h                the shared part
//   this file                     the helpers, the tests of the cache
//                                 alone and main
//   ..._switch.cpp                a switch between two cached variants,
//                                 and a lookup that misses
//   ..._migration.cpp             the data that crosses a switch
//   ..._repeated.cpp              switches back and forth

#include "region_cache.h"

#include <crucible/Vigil.h>
#include <foundation/effects/Effect.h>
#include "test_harness.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <bit>
#include <cstdint>
#include <cstdio>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

namespace test_region_cache {

void* fake_ptr(uint32_t variant, uint32_t iter, uint32_t op) {
    return std::bit_cast<void*>(
        static_cast<std::uintptr_t>((variant + 1) * 0x10000000ULL + (iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

namespace {

// Variant 0 is A and variant 1 is B.
int64_t tensor_size(uint32_t variant, uint32_t op_idx) {
    if (variant == 0) return 1024;
    return (op_idx < 3) ? 1024 : 2048;
}

}  // namespace

TensorMeta make_meta(void* data_ptr, int64_t size) {
    TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = ::crucible::tensor_dim(size);
    m.strides[0] = ::crucible::tensor_dim(1);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.layout = Layout::Strided;
    m.data_ptr = external_data_ptr(data_ptr);
    return m;
}

OpData make_op(const ShapeHash* shapes, uint32_t variant, uint32_t iter, uint32_t op_idx) {
    OpData d;
    d.entry.schema_hash = SCHEMA[op_idx];
    d.entry.shape_hash = shapes[op_idx];
    d.entry.num_inputs = (op_idx == 0) ? 0 : 1;
    d.entry.num_outputs = 1;

    uint16_t idx = 0;
    if (op_idx > 0) {
        int64_t in_sz = tensor_size(variant, op_idx - 1);
        d.metas.set(::fixy::Index<2>::check(idx++).expect("a test reads an element at or past the count of its array"),
                    make_meta(fake_ptr(variant, iter, op_idx - 1), in_sz));
    }
    int64_t out_sz = tensor_size(variant, op_idx);
    d.metas.set(::fixy::Index<2>::check(idx++).expect("a test reads an element at or past the count of its array"),
                make_meta(fake_ptr(variant, iter, op_idx), out_sz));
    d.n_metas = static_cast<uint16_t>(d.entry.num_inputs + d.entry.num_outputs);
    return d;
}

void feed_record(Vigil& vigil, const ShapeHash* shapes, uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(shapes, variant, iter, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_op(d.entry, d.metas, d.n_metas));
    }
}

void feed_trigger(Vigil& vigil, const ShapeHash* shapes, uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(shapes, variant, iter, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_op(d.entry, d.metas, d.n_metas));
    }
}

void align_and_activate(Vigil& vigil, const ShapeHash* shapes, uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < K; i++) {
        auto d = make_op(shapes, variant, iter, i);
        auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, r.action == DispatchResult::Action::RECORD && "alignment ops should return RECORD");
    }
    CRUCIBLE_TEST_ASSERT(kTest,
                         vigil.context().is_compiled() && "CrucibleContext should be compiled after K alignment ops");

    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(shapes, variant, iter, i);
        auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, r.action == DispatchResult::Action::COMPILED);
    }
}

namespace {

// A region of no ops with the content hash, which a frame on the arena builds.
[[nodiscard]] DagRef<RegionNode> make_hashed_region(::foundation::effects::Alloc alloc,
                                                    ::fixy::BumpArena<DagArena>& arena, ContentHash hash) {
    return ::foundation::core::with_arena_frame(arena, [alloc, hash](auto& frame) noexcept {
        auto no_ops = frame.template build_array<TraceEntry>(alloc, ::fixy::ElementCount{0},
                                                             [](::fixy::RunStart) noexcept { return TraceEntry{}; });
        const auto region = make_region(alloc, frame, no_ops);
        region->content_hash = hash;
        return region.weaken();
    });
}

void test_cache_dedup_and_cap() {
    auto test = ::foundation::effects::testing::test();
    RegionCache cache;

    CRUCIBLE_TEST_ASSERT(kTest, cache.size() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, cache.empty());

    ::fixy::BumpArena<DagArena> arena =
        ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{4096});
    const DagRef<RegionNode> region = make_hashed_region(test.alloc, arena, ContentHash{0x1234});

    cache.insert(*region);
    CRUCIBLE_TEST_ASSERT(kTest, cache.size() == 1);

    // One content hash occupies one slot however often it is inserted.
    cache.insert(*region);
    CRUCIBLE_TEST_ASSERT(kTest, cache.size() == 1);

    for (uint32_t i = 1; i < RegionCache::CAP; i++) {
        const DagRef<RegionNode> r = make_hashed_region(test.alloc, arena, ContentHash{0x1234 + i});
        cache.insert(*r);
    }
    CRUCIBLE_TEST_ASSERT(kTest, cache.size() == RegionCache::CAP);

    // Insertion past the capacity evicts the oldest entry instead of
    // growing.
    const DagRef<RegionNode> overflow = make_hashed_region(test.alloc, arena, ContentHash{0xFFFF});
    cache.insert(*overflow);
    CRUCIBLE_TEST_ASSERT(kTest, cache.size() == RegionCache::CAP);

    CRUCIBLE_TEST_ASSERT(kTest, cache.find(ContentHash{0xFFFF}).is_some());

    // 0x1234 went in first, so it is the one that leaves.
    CRUCIBLE_TEST_ASSERT(kTest, cache.find(ContentHash{0x1234}).is_none());

    ::fixy::check::pass(kTest, "  test_cache_dedup_and_cap: PASSED\n");
}

// find_alternate reads the plan off the region, not off a snapshot taken
// when the region was inserted.
//
// The distinction used to be invisible because the two agreed for every
// region that never changed, and it decided everything for a region whose
// plan arrived after it was cached: insert stored a zero op count for such a
// region, the position bound rejected the zero, and the entry was unfindable
// for the rest of its life in the cache. The one path that could repair it,
// notify_plan_ready, had no caller anywhere in the tree.
//
// Every production insert happens to go through CrucibleContext::activate,
// which refuses a planless region, so the poisoned state was unreachable
// from the runtime and the repair path had nothing to repair. That is an
// argument for deleting the repair path, not for keeping a cache whose
// entries can be silently unfindable: insert is public and takes any region.
void test_find_alternate_tracks_live_plan() {
    auto test = ::foundation::effects::testing::test();
    RegionCache cache;
    ::fixy::BumpArena<DagArena> arena =
        ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 14});

    // One frame on the arena builds the region and, after the insert, its
    // plan.
    ::foundation::core::with_arena_frame(arena, [alloc = test.alloc, &cache](auto& frame) noexcept {
        auto ops_run = frame.array(alloc, std::size_t{2}, TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        ::fixy::View<TraceEntry, 2> const ops = ops_run.view().template window<2>(0).expect("the run holds two ops");
        TraceEntry& op0 = ops.at(::foundation::core::index<0>());
        TraceEntry& op1 = ops.at(::foundation::core::index<1>());
        op0.schema_hash = SchemaHash{0x900};
        op0.shape_hash = ShapeHash{0x901};
        op1.schema_hash = SchemaHash{0x902};
        op1.shape_hash = ShapeHash{0x903};

        const auto made = make_region(alloc, frame, ops_run);
        const DagRef<RegionNode> region = made.weaken();
        CRUCIBLE_TEST_ASSERT(kTest, region->plan.is_none());

        cache.insert(*region);
        CRUCIBLE_TEST_ASSERT(kTest, cache.size() == 1);
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(cache.find(region->content_hash), ::fixy::some(region))
                                        && "a planless region is still cached and still findable by hash");

        // Not eligible to switch to: without a plan the context cannot enter
        // compiled mode, so offering it would strand the caller.
        CRUCIBLE_TEST_ASSERT(kTest, cache.find_alternate(0, op0.schema_hash, op0.shape_hash).is_none());
        CRUCIBLE_TEST_ASSERT(kTest, cache.find_alternate(1, op1.schema_hash, op1.shape_hash).is_none());

        // The plan arrives after the insert. No second call into the cache.
        auto slots_run = frame.array(alloc, std::size_t{1}, TensorSlot{});
        // A plain View of an arena run: the View carries no brand of its arena.
        ::fixy::View<TensorSlot, 1> const slots =
            slots_run.view().template window<1>(0).expect("the run holds one slot");
        TensorSlot& slot = slots.at(::foundation::core::index<0>());
        slot.nbytes = 4096;
        slot.birth_op = OpIndex{0};
        slot.death_op = OpIndex{1};
        slot.slot_id = SlotId{0};
        const auto plan = frame.template make<MemoryPlan>(alloc);
        plan->slots = slots_run.weaken();
        plan->pool_bytes = 4096;
        made->plan = make_plan_link(plan.weaken());

        CRUCIBLE_TEST_ASSERT(
            kTest, ::fixy::same_target(cache.find_alternate(0, op0.schema_hash, op0.shape_hash), ::fixy::some(region))
                       && "a region cached before its plan existed stayed unfindable after the plan "
                          "arrived: find_alternate is reading an insert-time snapshot again, and the "
                          "repair path for it has no caller");
        CRUCIBLE_TEST_ASSERT(
            kTest, ::fixy::same_target(cache.find_alternate(1, op1.schema_hash, op1.shape_hash), ::fixy::some(region)));

        // The position bound now means one thing: how many ops the region has.
        CRUCIBLE_TEST_ASSERT(kTest, cache.find_alternate(2, op1.schema_hash, op1.shape_hash).is_none());
        // A matching position with the wrong shape is still a miss.
        CRUCIBLE_TEST_ASSERT(kTest, cache.find_alternate(0, op0.schema_hash, ShapeHash{0xDEAD}).is_none());
        // And exclude still excludes.
        CRUCIBLE_TEST_ASSERT(
            kTest, cache.find_alternate(0, op0.schema_hash, op0.shape_hash, ConstRegionLink::some(::fixy::to(*region)))
                       .is_none());
    });

    ::fixy::check::pass(kTest, "  test_find_alternate_tracks_live_plan: PASSED\n");
}

}  // namespace

}  // namespace test_region_cache

int main() {
    using namespace test_region_cache;
    ::fixy::report(::fixy::Sink::Out, "test_region_cache:\n");
    test_cache_dedup_and_cap();
    test_find_alternate_tracks_live_plan();
    test_cache_miss_fallback();
    test_cache_switch_mid_iter();
    test_cache_data_migration();
    test_cache_repeated_switching();
    ::fixy::check::pass(kTest, "test_region_cache: all tests passed\n");
    return 0;
}
