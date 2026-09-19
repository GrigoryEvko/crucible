#include <crucible/Vigil.h>
#include <crucible/effects/_Capabilities.h>
#include "test_harness.h"
#include "test_assert.h"
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace crucible;

static constexpr uint32_t NUM_OPS = 8;
static constexpr uint32_t K = Vigil::ALIGNMENT_K;  // 5

// Both variants share one set of schemas, so only the shapes diverge.
static constexpr SchemaHash SCHEMA[NUM_OPS] = {SchemaHash{0x100}, SchemaHash{0x101}, SchemaHash{0x102},
                                               SchemaHash{0x103}, SchemaHash{0x104}, SchemaHash{0x105},
                                               SchemaHash{0x106}, SchemaHash{0x107}};

// Variant A holds every tensor at 1024 elements, which is 4096 bytes.
static constexpr ShapeHash SHAPE_A[NUM_OPS] = {ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x203},
                                               ShapeHash{0x204}, ShapeHash{0x205}, ShapeHash{0x206}, ShapeHash{0x207}};

// Variant B keeps A's first three shapes and changes the rest to 2048
// elements.  Both the shape hash and the tensor shapes differ there, so
// the two variants get different content hashes while sharing a prefix.
static constexpr ShapeHash SHAPE_B[NUM_OPS] = {ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x303},
                                               ShapeHash{0x304}, ShapeHash{0x305}, ShapeHash{0x306}, ShapeHash{0x307}};

static void* fake_ptr(uint32_t variant, uint32_t iter, uint32_t op) {
    return std::bit_cast<void*>(
        static_cast<std::uintptr_t>((variant + 1) * 0x10000000ULL + (iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

// Variant 0 is A and variant 1 is B.
static int64_t tensor_size(uint32_t variant, uint32_t op_idx) {
    if (variant == 0) return 1024;
    return (op_idx < 3) ? 1024 : 2048;
}

static TensorMeta make_meta(void* data_ptr, int64_t size) {
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

struct OpData {
    TraceRing::Entry entry{};
    TensorMeta metas[2]{};
    uint16_t n_metas = 0;
};

static OpData make_op(const ShapeHash* shapes, uint32_t variant, uint32_t iter, uint32_t op_idx) {
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

static void feed_record(Vigil& vigil, const ShapeHash* shapes, uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(shapes, variant, iter, i);
        (void)vigil.record_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    }
}

static void feed_trigger(Vigil& vigil, const ShapeHash* shapes, uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(shapes, variant, iter, i);
        (void)vigil.record_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    }
}

using test::flush_and_wait_compiled;

static void align_and_activate(Vigil& vigil, const ShapeHash* shapes, uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < K; i++) {
        auto d = make_op(shapes, variant, iter, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::RECORD && "alignment ops should return RECORD");
    }
    assert(vigil.context().is_compiled() && "CrucibleContext should be compiled after K alignment ops");

    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(shapes, variant, iter, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
}

// Once both variants are cached, a switch between them at the first
// diverging op must come out of the cache.
static void test_cache_switch_mid_iter() {
    Vigil vigil;

    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    assert(vigil.region_cache().size() == 1);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 4, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 5, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
    auto dB3 = make_op(SHAPE_B, 1, 5, 3);
    auto r_div = vigil.dispatch_op(crucible::vouch(dB3.entry), dB3.metas, dB3.n_metas);
    assert(r_div.action == DispatchResult::Action::RECORD);
    assert(r_div.status == ReplayStatus::DIVERGED);

    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, SHAPE_B, 1, iter);
    feed_trigger(vigil, SHAPE_B, 1, 16);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_B, 1, 17);

    assert(vigil.region_cache().size() == 2);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_B, 1, 18, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    // Ops 0 to 2 are the shared prefix, so the switch back to A can only
    // show up at op 3.
    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 19, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    auto dA3 = make_op(SHAPE_A, 0, 19, 3);
    auto r_switch = vigil.dispatch_op(crucible::vouch(dA3.entry), dA3.metas, dA3.n_metas);

    assert(r_switch.action == DispatchResult::Action::COMPILED && "Expected instant cache switch to variant A");

    for (uint32_t i = 4; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 19, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    std::printf("  test_cache_switch_mid_iter: PASSED\n");
}

// Output written before a switch must survive the move to the other
// region's pool.
static void test_cache_data_migration() {
    Vigil vigil;

    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 4, i);
        (void)vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    }
    auto dB3 = make_op(SHAPE_B, 1, 4, 3);
    auto r_div = vigil.dispatch_op(crucible::vouch(dB3.entry), dB3.metas, dB3.n_metas);
    assert(r_div.action == DispatchResult::Action::RECORD);

    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, SHAPE_B, 1, iter);
    feed_trigger(vigil, SHAPE_B, 1, 16);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_B, 1, 17);

    assert(vigil.region_cache().size() == 2);

    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_B, 1, 18, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
        // A prefix output holds 1024 floats, which is 4096 bytes.
        std::memset(vigil.output_ptr(0), static_cast<int>(0xA0 + i), 4096);
    }

    auto dA3 = make_op(SHAPE_A, 0, 18, 3);
    auto r_switch = vigil.dispatch_op(crucible::vouch(dA3.entry), dA3.metas, dA3.n_metas);
    assert(r_switch.action == DispatchResult::Action::COMPILED && "Expected cache switch from B to A at pos 3");

    // Op 3's input is op 2's output, so it must still carry the pattern
    // written for i equal to two.
    auto* in_data = static_cast<uint8_t*>(vigil.input_ptr(0));
    for (uint32_t b = 0; b < 4096; b++) {
        assert(in_data[b] == 0xA2 && "Data migration failed: op 2's output not preserved");
    }

    for (uint32_t i = 4; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 18, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    std::printf("  test_cache_data_migration: PASSED\n");
}

static void test_cache_miss_fallback() {
    Vigil vigil;

    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    // A schema no cached region carries, so the lookup must miss.
    TraceRing::Entry bad{};
    bad.schema_hash = SchemaHash{0xDEAD};
    bad.shape_hash = ShapeHash{0xBEEF};
    bad.num_inputs = 0;
    bad.num_outputs = 1;
    TensorMeta bad_meta = make_meta(fake_ptr(99, 99, 0), 512);

    auto r = vigil.dispatch_op(crucible::vouch(bad), &bad_meta, 1);
    assert(r.action == DispatchResult::Action::RECORD);
    assert(r.status == ReplayStatus::DIVERGED);
    assert(!vigil.context().is_compiled());
    assert(vigil.diverged_count() == 1);

    assert(vigil.region_cache().size() == 1);

    std::printf("  test_cache_miss_fallback: PASSED\n");
}

static void test_cache_dedup_and_cap() {
    auto test = effects::testing::test();
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

    std::printf("  test_cache_dedup_and_cap: PASSED\n");
}

// This is the alternating-batch-size case.  Once both regions are
// cached, every switch must come from the cache and none may fall back
// to recording.
static void test_cache_repeated_switching() {
    Vigil vigil;

    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 4, i);
        (void)vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    }

    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 5, i);
        (void)vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    }
    auto dB3 = make_op(SHAPE_B, 1, 5, 3);
    (void)vigil.dispatch_op(crucible::vouch(dB3.entry), dB3.metas, dB3.n_metas);

    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, SHAPE_B, 1, iter);
    feed_trigger(vigil, SHAPE_B, 1, 16);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_B, 1, 17);

    assert(vigil.region_cache().size() == 2);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_B, 1, 18, i);
        (void)vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    }

    for (uint32_t cycle = 0; cycle < 4; cycle++) {
        const ShapeHash* active_shapes = (cycle % 2 == 0) ? SHAPE_A : SHAPE_B;
        uint32_t variant = (cycle % 2 == 0) ? 0 : 1;
        uint32_t iter = 20 + cycle;

        for (uint32_t i = 0; i < 3; i++) {
            auto d = make_op(active_shapes, variant, iter, i);
            auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }

        auto d3 = make_op(active_shapes, variant, iter, 3);
        auto r3 = vigil.dispatch_op(crucible::vouch(d3.entry), d3.metas, d3.n_metas);
        assert(r3.action == DispatchResult::Action::COMPILED && "Cache switch failed during repeated alternation");

        for (uint32_t i = 4; i < NUM_OPS; i++) {
            auto d = make_op(active_shapes, variant, iter, i);
            auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }
    }

    std::printf("  test_cache_repeated_switching: PASSED\n");
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
static void test_find_alternate_tracks_live_plan() {
    auto test = effects::testing::test();
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

    std::printf("  test_find_alternate_tracks_live_plan: PASSED\n");
}

int main() {
    std::printf("test_region_cache:\n");
    test_cache_dedup_and_cap();
    test_find_alternate_tracks_live_plan();
    test_cache_miss_fallback();
    test_cache_switch_mid_iter();
    test_cache_data_migration();
    test_cache_repeated_switching();
    std::printf("test_region_cache: all tests passed\n");
    return 0;
}
