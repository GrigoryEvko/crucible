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
#include "test_assert.h"
#include <bit>
#include <cstdint>
#include <cstdio>

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
        d.metas[idx++] = make_meta(fake_ptr(variant, iter, op_idx - 1), in_sz);
    }
    int64_t out_sz = tensor_size(variant, op_idx);
    d.metas[idx++] = make_meta(fake_ptr(variant, iter, op_idx), out_sz);
    d.n_metas = static_cast<uint16_t>(d.entry.num_inputs + d.entry.num_outputs);
    return d;
}

void feed_record(Vigil& vigil, const ShapeHash* shapes, uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(shapes, variant, iter, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
    }
}

void feed_trigger(Vigil& vigil, const ShapeHash* shapes, uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(shapes, variant, iter, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
    }
}

void align_and_activate(Vigil& vigil, const ShapeHash* shapes, uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < K; i++) {
        auto d = make_op(shapes, variant, iter, i);
        auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::RECORD && "alignment ops should return RECORD");
    }
    assert(vigil.context().is_compiled() && "CrucibleContext should be compiled after K alignment ops");

    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(shapes, variant, iter, i);
        auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
}

namespace {

void test_cache_dedup_and_cap() {
    auto test = ::foundation::effects::testing::test();
    RegionCache cache;

    assert(cache.size() == 0);
    assert(cache.empty());

    Arena arena(4096);
    auto* region = make_region(test.alloc, arena, nullptr, 0);
    region->content_hash = ContentHash{0x1234};

    cache.insert(region);
    assert(cache.size() == 1);

    // One content hash occupies one slot however often it is inserted.
    cache.insert(region);
    assert(cache.size() == 1);

    for (uint32_t i = 1; i < RegionCache::CAP; i++) {
        auto* r = make_region(test.alloc, arena, nullptr, 0);
        r->content_hash = ContentHash{0x1234 + i};
        cache.insert(r);
    }
    assert(cache.size() == RegionCache::CAP);

    // Insertion past the capacity evicts the oldest entry instead of
    // growing.
    auto* overflow = make_region(test.alloc, arena, nullptr, 0);
    overflow->content_hash = ContentHash{0xFFFF};
    cache.insert(overflow);
    assert(cache.size() == RegionCache::CAP);

    assert(cache.find(ContentHash{0xFFFF}) != nullptr);

    // 0x1234 went in first, so it is the one that leaves.
    assert(cache.find(ContentHash{0x1234}) == nullptr);

    crucible::test::pass("  test_cache_dedup_and_cap: PASSED\n");
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
    Arena arena(1 << 14);

    TraceEntry ops[2]{};
    ops[0].schema_hash = SchemaHash{0x900};
    ops[0].shape_hash = ShapeHash{0x901};
    ops[1].schema_hash = SchemaHash{0x902};
    ops[1].shape_hash = ShapeHash{0x903};

    auto* region = make_region(test.alloc, arena, ops, 2);
    assert(region->plan == nullptr);

    cache.insert(region);
    assert(cache.size() == 1);
    assert(cache.find(region->content_hash) == region
           && "a planless region is still cached and still findable by hash");

    // Not eligible to switch to: without a plan the context cannot enter
    // compiled mode, so offering it would strand the caller.
    assert(cache.find_alternate(0, ops[0].schema_hash, ops[0].shape_hash) == nullptr);
    assert(cache.find_alternate(1, ops[1].schema_hash, ops[1].shape_hash) == nullptr);

    // The plan arrives after the insert. No second call into the cache.
    TensorSlot slots[1]{};
    slots[0].nbytes = 4096;
    slots[0].birth_op = OpIndex{0};
    slots[0].death_op = OpIndex{1};
    slots[0].slot_id = SlotId{0};
    auto* plan = arena.alloc_obj<MemoryPlan>(test.alloc);
    ::new(plan) MemoryPlan{};
    plan->slots = slots;
    plan->num_slots = 1;
    plan->pool_bytes = 4096;
    region->plan = plan;

    assert(cache.find_alternate(0, ops[0].schema_hash, ops[0].shape_hash) == region
           && "a region cached before its plan existed stayed unfindable after the plan "
              "arrived: find_alternate is reading an insert-time snapshot again, and the "
              "repair path for it has no caller");
    assert(cache.find_alternate(1, ops[1].schema_hash, ops[1].shape_hash) == region);

    // The position bound now means one thing: how many ops the region has.
    assert(cache.find_alternate(2, ops[1].schema_hash, ops[1].shape_hash) == nullptr);
    // A matching position with the wrong shape is still a miss.
    assert(cache.find_alternate(0, ops[0].schema_hash, ShapeHash{0xDEAD}) == nullptr);
    // And exclude still excludes.
    assert(cache.find_alternate(0, ops[0].schema_hash, ops[0].shape_hash, region) == nullptr);

    crucible::test::pass("  test_find_alternate_tracks_live_plan: PASSED\n");
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
    crucible::test::pass("test_region_cache: all tests passed\n");
    return 0;
}
