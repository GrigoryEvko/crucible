#include <crucible/ReplayEngine.h>
#include <crucible/BackgroundThread.h>
#include <foundation/effects/Effect.h>
#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

// The views of the replay chain are minted on the thread that holds a
// Vigil's producer claim.  These tests run the chain without a Vigil, so
// they take that context from the test door.
static constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

using crucible::ReplayEngine;
using crucible::ReplayStatus;
using crucible::PoolAllocator;
using crucible::MemoryPlan;
using crucible::TensorSlot;
using crucible::TraceEntry;
using crucible::RegionNode;
using crucible::TraceNode;
using crucible::TraceNodeKind;
using crucible::SlotId;
using crucible::OpIndex;
using crucible::ScalarType;
using crucible::DeviceType;
using crucible::Layout;
using crucible::SchemaHash;
using crucible::ShapeHash;

// Of the entry fields a caller fills in, only schema_hash, shape_hash, the
// two counts and the two arrays of slot ids reach the replay engine.  The
// rest are left at their defaults on purpose.
static void init_region(RegionNode* r, ::fixy::View<TraceEntry> ops) {
    ::new(r) RegionNode{};
    r->kind = TraceNodeKind::REGION;
    r->ops = ops;
}

// Every slot is internal and 256 bytes wide, so slot i sits at offset
// i * 256 and no two slots overlap.
static MemoryPlan make_simple_plan(::fixy::View<TensorSlot> slots) {
    const uint32_t n = static_cast<uint32_t>(slots.size());
    uint32_t i = 0;
    for (TensorSlot& slot : slots) {
        slot = {.offset_bytes = i * 256,
                .nbytes = 256,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{n},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{i},
                .pad2 = {}};
        ++i;
    }
    MemoryPlan plan{};
    plan.slots = slots;
    plan.num_external = 0;
    plan.pool_bytes = n * 256;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;
    return plan;
}

static void test_linear_match() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{4096};

    ::foundation::core::FixedArray<TraceEntry, 3> ops{};
    for (uint32_t i = 0; i < 3; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{100 + i};
        op.shape_hash = ShapeHash{200 + i};
        op.set_num_outputs(1);
        op.set_output_slot_ids(crucible::test::run_of(test.alloc, arena, SlotId{i}));
        op.set_num_inputs(0);
    }

    RegionNode region{};
    init_region(&region, ops.view());

    ::foundation::core::FixedArray<TensorSlot, 3> slots{};
    auto plan = make_simple_plan(slots.view());
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    ReplayEngine engine;
    engine.init(region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view(kVigilForeground);

    assert(engine.is_initialized());
    assert(engine.num_ops() == 3);
    assert(engine.ops_matched() == 0);
    assert(!engine.is_complete());

    // Every op but the last reports MATCH and the last reports COMPLETE,
    // which is why the final step is unrolled out of the loop.
    for (uint32_t i = 0; i < 2; i++) {
        auto status = engine.advance(SchemaHash{100 + i}, ShapeHash{200 + i}, av);
        assert(status == ReplayStatus::MATCH);
        assert(engine.ops_matched() == i + 1);
        assert(engine.matched_op_index().value() == OpIndex{i});

        void* ptr = engine.output_ptr(0, av);
        assert(ptr == pool.slot_ptr(SlotId{i}, pv));
    }

    {
        auto status = engine.advance(SchemaHash{102}, ShapeHash{202}, av);
        assert(status == ReplayStatus::COMPLETE);
        assert(engine.ops_matched() == 3);
        assert(engine.matched_op_index().value() == OpIndex{2});
        void* ptr = engine.output_ptr(0, av);
        assert(ptr == pool.slot_ptr(SlotId{2}, pv));
    }
    assert(engine.is_complete());

    crucible::test::pass("  test_linear_match: PASSED\n");
}

static void test_schema_divergence() {
    ::foundation::core::FixedArray<SlotId, 1> out_slot{SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 3> ops{};
    for (uint32_t i = 0; i < 3; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{100 + i};
        op.shape_hash = ShapeHash{200};
        op.set_num_outputs(1);
        op.set_output_slot_ids(out_slot.view());
    }

    RegionNode region{};
    init_region(&region, ops.view());

    ::foundation::core::FixedArray<TensorSlot, 1> slots{};
    auto plan = make_simple_plan(slots.view());
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine engine;
    engine.init(region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view(kVigilForeground);
    assert(engine.advance(SchemaHash{100}, ShapeHash{200}, av) == ReplayStatus::MATCH);

    assert(engine.advance(SchemaHash{999}, ShapeHash{200}, av) == ReplayStatus::DIVERGED);
    assert(engine.diverged_op_index_tagged().value() == OpIndex{1});
    assert(engine.ops_matched() == 1);

    // The position stays on the diverged op, so a second advance
    // diverges again instead of stepping over it.
    assert(engine.advance(SchemaHash{999}, ShapeHash{200}, av) == ReplayStatus::DIVERGED);
    assert(engine.diverged_op_index_tagged().value() == OpIndex{1});

    crucible::test::pass("  test_schema_divergence: PASSED\n");
}

static void test_shape_divergence() {
    ::foundation::core::FixedArray<SlotId, 1> out_slot{SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 2> ops{};
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{100};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{200};
    ops.at(::foundation::core::index<0>()).set_num_outputs(1);
    ops.at(::foundation::core::index<0>()).set_output_slot_ids(out_slot.view());
    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{101};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{201};
    ops.at(::foundation::core::index<1>()).set_num_outputs(1);
    ops.at(::foundation::core::index<1>()).set_output_slot_ids(out_slot.view());

    RegionNode region{};
    init_region(&region, ops.view());

    ::foundation::core::FixedArray<TensorSlot, 1> slots{};
    auto plan = make_simple_plan(slots.view());
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine engine;
    engine.init(region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view(kVigilForeground);
    assert(engine.advance(SchemaHash{100}, ShapeHash{200}, av) == ReplayStatus::MATCH);

    // The schema still matches here, so only the shape can be the cause
    // of the divergence.
    assert(engine.advance(SchemaHash{101}, ShapeHash{999}, av) == ReplayStatus::DIVERGED);
    assert(engine.diverged_op_index_tagged().value() == OpIndex{1});

    crucible::test::pass("  test_shape_divergence: PASSED\n");
}

static void test_reset() {
    ::foundation::core::FixedArray<SlotId, 1> out_slot{SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 2> ops{};
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{10};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{20};
    ops.at(::foundation::core::index<0>()).set_num_outputs(1);
    ops.at(::foundation::core::index<0>()).set_output_slot_ids(out_slot.view());
    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{11};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{21};
    ops.at(::foundation::core::index<1>()).set_num_outputs(1);
    ops.at(::foundation::core::index<1>()).set_output_slot_ids(out_slot.view());

    RegionNode region{};
    init_region(&region, ops.view());

    ::foundation::core::FixedArray<TensorSlot, 1> slots{};
    auto plan = make_simple_plan(slots.view());
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine engine;
    engine.init(region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view(kVigilForeground);
    assert(engine.advance(SchemaHash{10}, ShapeHash{20}, av) == ReplayStatus::MATCH);
    assert(engine.advance(SchemaHash{11}, ShapeHash{21}, av) == ReplayStatus::COMPLETE);
    assert(engine.is_complete());

    engine.reset(av);
    assert(!engine.is_complete());
    assert(engine.ops_matched() == 0);

    assert(engine.advance(SchemaHash{10}, ShapeHash{20}, av) == ReplayStatus::MATCH);
    assert(engine.advance(SchemaHash{11}, ShapeHash{21}, av) == ReplayStatus::COMPLETE);
    assert(engine.is_complete());

    crucible::test::pass("  test_reset: PASSED\n");
}

// Op 1 reads the slot op 0 writes, so the two ops alias one pool
// region through different entries.
static void test_input_ptr() {
    ::foundation::core::FixedArray<SlotId, 1> out0{SlotId{0}};
    ::foundation::core::FixedArray<SlotId, 1> out1{SlotId{1}};
    ::foundation::core::FixedArray<SlotId, 1> in1{SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 2> ops{};
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{50};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{60};
    ops.at(::foundation::core::index<0>()).set_num_outputs(1);
    ops.at(::foundation::core::index<0>()).set_output_slot_ids(out0.view());
    ops.at(::foundation::core::index<0>()).set_num_inputs(0);

    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{51};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{61};
    ops.at(::foundation::core::index<1>()).set_num_outputs(1);
    ops.at(::foundation::core::index<1>()).set_output_slot_ids(out1.view());
    ops.at(::foundation::core::index<1>()).set_num_inputs(1);
    ops.at(::foundation::core::index<1>()).set_input_slot_ids(in1.view());

    RegionNode region{};
    init_region(&region, ops.view());

    ::foundation::core::FixedArray<TensorSlot, 2> slots{};
    auto plan = make_simple_plan(slots.view());
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine engine;
    engine.init(region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view(kVigilForeground);
    assert(engine.advance(SchemaHash{50}, ShapeHash{60}, av) == ReplayStatus::MATCH);
    assert(engine.output_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));

    assert(engine.advance(SchemaHash{51}, ShapeHash{61}, av) == ReplayStatus::COMPLETE);
    assert(engine.output_ptr(0, av) == pool.slot_ptr(SlotId{1}, pv));
    assert(engine.input_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));

    assert(engine.input_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));

    crucible::test::pass("  test_input_ptr: PASSED\n");
}

static void test_invalid_slot() {
    ::foundation::core::FixedArray<SlotId, 2> out{SlotId{0}, SlotId::none()};

    ::foundation::core::FixedArray<TraceEntry, 1> ops{};
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{77};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{88};
    ops.at(::foundation::core::index<0>()).set_num_outputs(2);
    ops.at(::foundation::core::index<0>()).set_output_slot_ids(out.view());

    RegionNode region{};
    init_region(&region, ops.view());

    ::foundation::core::FixedArray<TensorSlot, 1> slots{};
    auto plan = make_simple_plan(slots.view());
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine engine;
    engine.init(region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view(kVigilForeground);
    assert(engine.advance(SchemaHash{77}, ShapeHash{88}, av) == ReplayStatus::COMPLETE);

    assert(engine.output_ptr(0, av) != nullptr);
    assert(engine.output_ptr(1, av) == nullptr);

    crucible::test::pass("  test_invalid_slot: PASSED\n");
}

static void test_current_entry() {
    ::foundation::core::FixedArray<SlotId, 1> out{SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 3> ops{};
    for (uint32_t i = 0; i < 3; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{300 + i};
        op.shape_hash = ShapeHash{400 + i};
        op.set_num_outputs(1);
        op.set_output_slot_ids(out.view());
    }

    RegionNode region{};
    init_region(&region, ops.view());

    ::foundation::core::FixedArray<TensorSlot, 1> slots{};
    auto plan = make_simple_plan(slots.view());
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine engine;
    engine.init(region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view(kVigilForeground);
    for (uint32_t i = 0; i < 3; i++) {
        auto s = engine.advance(SchemaHash{300 + i}, ShapeHash{400 + i}, av);
        assert(s == (i < 2 ? ReplayStatus::MATCH : ReplayStatus::COMPLETE));
        const auto& entry = engine.current_entry();
        assert(entry.schema_hash == SchemaHash{300 + i});
        assert(entry.shape_hash == ShapeHash{400 + i});
    }

    crucible::test::pass("  test_current_entry: PASSED\n");
}

// The plan here comes from the real sweep-line planner rather than the
// fixed-stride helper above, so the offsets are whatever the planner
// chooses and the assertions never name one.
static void test_integration_with_pool() {
    auto test = ::foundation::effects::testing::test();
    crucible::BackgroundThread bt;

    // Slot 2 is external: it stands for a parameter whose storage the
    // pool does not own and must not place.
    constexpr uint32_t NSLOTS = 3;
    ::foundation::core::FixedArray<TensorSlot, NSLOTS> slots{};
    slots.at(::foundation::core::index<0>()) = {.offset_bytes = 0,
                                                .nbytes = 512,
                                                .birth_op = OpIndex{0},
                                                .death_op = OpIndex{2},
                                                .dtype = ScalarType::Float,
                                                .device_type = DeviceType::CPU,
                                                .device_idx = 0,
                                                .layout = Layout::Strided,
                                                .is_external = false,
                                                .pad = {},
                                                .slot_id = SlotId{0},
                                                .pad2 = {}};
    slots.at(::foundation::core::index<1>()) = {.offset_bytes = 0,
                                                .nbytes = 256,
                                                .birth_op = OpIndex{1},
                                                .death_op = OpIndex{2},
                                                .dtype = ScalarType::Float,
                                                .device_type = DeviceType::CPU,
                                                .device_idx = 0,
                                                .layout = Layout::Strided,
                                                .is_external = false,
                                                .pad = {},
                                                .slot_id = SlotId{1},
                                                .pad2 = {}};
    slots.at(::foundation::core::index<2>()) = {.offset_bytes = 0,
                                                .nbytes = 128,
                                                .birth_op = OpIndex{0},
                                                .death_op = OpIndex{2},
                                                .dtype = ScalarType::Float,
                                                .device_type = DeviceType::CPU,
                                                .device_idx = 0,
                                                .layout = Layout::Strided,
                                                .is_external = true,
                                                .pad = {},
                                                .slot_id = SlotId{2},
                                                .pad2 = {}};

    crucible::MemoryPlan plan_storage{};
    ::fixy::BumpArena sweep_arena{size_t{1} << 16};
    auto* plan = bt.compute_memory_plan(test.alloc, plan_storage, sweep_arena, slots.view());
    assert(plan != nullptr);

    PoolAllocator pool;
    pool.init(plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    alignas(256) char fake_param[128];
    pool.register_external(SlotId{2}, ::fixy::mint_refined<::fixy::non_null, void*>(fake_param), pv);

    ::foundation::core::FixedArray<SlotId, 1> op0_out{SlotId{0}};
    ::foundation::core::FixedArray<SlotId, 1> op0_in{SlotId{2}};
    ::foundation::core::FixedArray<SlotId, 1> op1_out{SlotId{1}};
    ::foundation::core::FixedArray<SlotId, 1> op1_in{SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 2> ops{};
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{0xAAAA};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{0xBBBB};
    ops.at(::foundation::core::index<0>()).set_num_outputs(1);
    ops.at(::foundation::core::index<0>()).set_output_slot_ids(op0_out.view());
    ops.at(::foundation::core::index<0>()).set_num_inputs(1);
    ops.at(::foundation::core::index<0>()).set_input_slot_ids(op0_in.view());

    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{0xCCCC};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{0xDDDD};
    ops.at(::foundation::core::index<1>()).set_num_outputs(1);
    ops.at(::foundation::core::index<1>()).set_output_slot_ids(op1_out.view());
    ops.at(::foundation::core::index<1>()).set_num_inputs(1);
    ops.at(::foundation::core::index<1>()).set_input_slot_ids(op1_in.view());

    RegionNode region{};
    init_region(&region, ops.view());

    ReplayEngine engine;
    engine.init(region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view(kVigilForeground);
    assert(engine.advance(SchemaHash{0xAAAA}, ShapeHash{0xBBBB}, av) == ReplayStatus::MATCH);
    assert(engine.output_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));
    // The external slot resolves to registered storage, not into the pool.
    assert(engine.input_ptr(0, av) == fake_param);

    std::memset(engine.output_ptr(0, av), 0x11, 512);

    assert(engine.advance(SchemaHash{0xCCCC}, ShapeHash{0xDDDD}, av) == ReplayStatus::COMPLETE);
    assert(engine.output_ptr(0, av) == pool.slot_ptr(SlotId{1}, pv));
    assert(engine.input_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));

    // Reading op 1's input back finds op 0's fill, which is what proves
    // the two entries name the same pool bytes.
    auto* p = static_cast<uint8_t*>(engine.input_ptr(0, av));
    for (uint32_t i = 0; i < 512; i++)
        assert(p[i] == 0x11);

    assert(engine.is_complete());

    engine.reset(av);
    assert(!engine.is_complete());
    assert(engine.advance(SchemaHash{0xAAAA}, ShapeHash{0xBBBB}, av) == ReplayStatus::MATCH);
    assert(engine.advance(SchemaHash{0xCCCC}, ShapeHash{0xDDDD}, av) == ReplayStatus::COMPLETE);
    assert(engine.is_complete());

    crucible::test::pass("  test_integration_with_pool: PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_replay_engine:\n");
    static_assert(std::is_same_v<ReplayEngine::PoolBorrow, ::fixy::BorrowedRef<const PoolAllocator>>);
    static_assert(sizeof(ReplayEngine::PoolBorrow) == sizeof(const PoolAllocator*));
    test_linear_match();
    test_schema_divergence();
    test_shape_divergence();
    test_reset();
    test_input_ptr();
    test_invalid_slot();
    test_current_entry();
    test_integration_with_pool();
    crucible::test::pass("test_replay_engine: all tests passed\n");
    return 0;
}
