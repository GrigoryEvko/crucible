#include <crucible/ReplayEngine.h>
#include <crucible/BackgroundThread.h>
#include <crucible/effects/Capabilities.h>
#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

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

// Of the entry fields a caller fills in, only schema_hash, shape_hash,
// num_outputs, output_slot_ids, num_inputs and input_slot_ids reach the
// replay engine.  The rest are left at their defaults on purpose.
static void init_region(RegionNode* r, TraceEntry* ops, uint32_t n) {
    ::new(r) RegionNode{};
    r->kind = TraceNodeKind::REGION;
    r->ops = ops;
    r->num_ops = n;
}

// Every slot is internal and 256 bytes wide, so slot i sits at offset
// i * 256 and no two slots overlap.
static MemoryPlan make_simple_plan(TensorSlot* slots, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        slots[i] = {.offset_bytes = i * 256,
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
    }
    MemoryPlan plan{};
    plan.slots = slots;
    plan.num_slots = n;
    plan.num_external = 0;
    plan.pool_bytes = n * 256;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;
    return plan;
}

static void test_linear_match() {
    SlotId out_slots[3][1] = {{SlotId{0}}, {SlotId{1}}, {SlotId{2}}};

    TraceEntry ops[3]{};
    for (uint32_t i = 0; i < 3; i++) {
        ops[i].schema_hash = SchemaHash{100 + i};
        ops[i].shape_hash = ShapeHash{200 + i};
        ops[i].num_outputs = 1;
        ops[i].output_slot_ids = out_slots[i];
        ops[i].num_inputs = 0;
        ops[i].input_slot_ids = nullptr;
    }

    RegionNode region{};
    init_region(&region, ops, 3);

    TensorSlot slots[3];
    auto plan = make_simple_plan(slots, 3);
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();

    ReplayEngine engine;
    engine.init(&region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view();

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

    std::printf("  test_linear_match: PASSED\n");
}

static void test_schema_divergence() {
    SlotId out_slot[1] = {SlotId{0}};

    TraceEntry ops[3]{};
    for (uint32_t i = 0; i < 3; i++) {
        ops[i].schema_hash = SchemaHash{100 + i};
        ops[i].shape_hash = ShapeHash{200};
        ops[i].num_outputs = 1;
        ops[i].output_slot_ids = out_slot;
    }

    RegionNode region{};
    init_region(&region, ops, 3);

    TensorSlot slots[1];
    auto plan = make_simple_plan(slots, 1);
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();
    ReplayEngine engine;
    engine.init(&region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view();
    assert(engine.advance(SchemaHash{100}, ShapeHash{200}, av) == ReplayStatus::MATCH);

    assert(engine.advance(SchemaHash{999}, ShapeHash{200}, av) == ReplayStatus::DIVERGED);
    assert(engine.diverged_op_index_tagged().value() == OpIndex{1});
    assert(engine.ops_matched() == 1);

    // The position stays on the diverged op, so a second advance
    // diverges again instead of stepping over it.
    assert(engine.advance(SchemaHash{999}, ShapeHash{200}, av) == ReplayStatus::DIVERGED);
    assert(engine.diverged_op_index_tagged().value() == OpIndex{1});

    std::printf("  test_schema_divergence: PASSED\n");
}

static void test_shape_divergence() {
    SlotId out_slot[1] = {SlotId{0}};

    TraceEntry ops[2]{};
    ops[0].schema_hash = SchemaHash{100};
    ops[0].shape_hash = ShapeHash{200};
    ops[0].num_outputs = 1;
    ops[0].output_slot_ids = out_slot;
    ops[1].schema_hash = SchemaHash{101};
    ops[1].shape_hash = ShapeHash{201};
    ops[1].num_outputs = 1;
    ops[1].output_slot_ids = out_slot;

    RegionNode region{};
    init_region(&region, ops, 2);

    TensorSlot slots[1];
    auto plan = make_simple_plan(slots, 1);
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();
    ReplayEngine engine;
    engine.init(&region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view();
    assert(engine.advance(SchemaHash{100}, ShapeHash{200}, av) == ReplayStatus::MATCH);

    // The schema still matches here, so only the shape can be the cause
    // of the divergence.
    assert(engine.advance(SchemaHash{101}, ShapeHash{999}, av) == ReplayStatus::DIVERGED);
    assert(engine.diverged_op_index_tagged().value() == OpIndex{1});

    std::printf("  test_shape_divergence: PASSED\n");
}

static void test_reset() {
    SlotId out_slot[1] = {SlotId{0}};

    TraceEntry ops[2]{};
    ops[0].schema_hash = SchemaHash{10};
    ops[0].shape_hash = ShapeHash{20};
    ops[0].num_outputs = 1;
    ops[0].output_slot_ids = out_slot;
    ops[1].schema_hash = SchemaHash{11};
    ops[1].shape_hash = ShapeHash{21};
    ops[1].num_outputs = 1;
    ops[1].output_slot_ids = out_slot;

    RegionNode region{};
    init_region(&region, ops, 2);

    TensorSlot slots[1];
    auto plan = make_simple_plan(slots, 1);
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();
    ReplayEngine engine;
    engine.init(&region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view();
    assert(engine.advance(SchemaHash{10}, ShapeHash{20}, av) == ReplayStatus::MATCH);
    assert(engine.advance(SchemaHash{11}, ShapeHash{21}, av) == ReplayStatus::COMPLETE);
    assert(engine.is_complete());

    engine.reset(av);
    assert(!engine.is_complete());
    assert(engine.ops_matched() == 0);

    assert(engine.advance(SchemaHash{10}, ShapeHash{20}, av) == ReplayStatus::MATCH);
    assert(engine.advance(SchemaHash{11}, ShapeHash{21}, av) == ReplayStatus::COMPLETE);
    assert(engine.is_complete());

    std::printf("  test_reset: PASSED\n");
}

// Op 1 reads the slot op 0 writes, so the two ops alias one pool
// region through different entries.
static void test_input_ptr() {
    SlotId out0[1] = {SlotId{0}};
    SlotId out1[1] = {SlotId{1}};
    SlotId in1[1] = {SlotId{0}};

    TraceEntry ops[2]{};
    ops[0].schema_hash = SchemaHash{50};
    ops[0].shape_hash = ShapeHash{60};
    ops[0].num_outputs = 1;
    ops[0].output_slot_ids = out0;
    ops[0].num_inputs = 0;
    ops[0].input_slot_ids = nullptr;

    ops[1].schema_hash = SchemaHash{51};
    ops[1].shape_hash = ShapeHash{61};
    ops[1].num_outputs = 1;
    ops[1].output_slot_ids = out1;
    ops[1].num_inputs = 1;
    ops[1].input_slot_ids = in1;

    RegionNode region{};
    init_region(&region, ops, 2);

    TensorSlot slots[2];
    auto plan = make_simple_plan(slots, 2);
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();
    ReplayEngine engine;
    engine.init(&region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view();
    assert(engine.advance(SchemaHash{50}, ShapeHash{60}, av) == ReplayStatus::MATCH);
    assert(engine.output_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));

    assert(engine.advance(SchemaHash{51}, ShapeHash{61}, av) == ReplayStatus::COMPLETE);
    assert(engine.output_ptr(0, av) == pool.slot_ptr(SlotId{1}, pv));
    assert(engine.input_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));

    assert(engine.input_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));

    std::printf("  test_input_ptr: PASSED\n");
}

static void test_invalid_slot() {
    SlotId out[2] = {SlotId{0}, SlotId::none()};

    TraceEntry ops[1]{};
    ops[0].schema_hash = SchemaHash{77};
    ops[0].shape_hash = ShapeHash{88};
    ops[0].num_outputs = 2;
    ops[0].output_slot_ids = out;

    RegionNode region{};
    init_region(&region, ops, 1);

    TensorSlot slots[1];
    auto plan = make_simple_plan(slots, 1);
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();
    ReplayEngine engine;
    engine.init(&region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view();
    assert(engine.advance(SchemaHash{77}, ShapeHash{88}, av) == ReplayStatus::COMPLETE);

    assert(engine.output_ptr(0, av) != nullptr);
    assert(engine.output_ptr(1, av) == nullptr);

    std::printf("  test_invalid_slot: PASSED\n");
}

static void test_current_entry() {
    SlotId out[1] = {SlotId{0}};

    TraceEntry ops[3]{};
    for (uint32_t i = 0; i < 3; i++) {
        ops[i].schema_hash = SchemaHash{300 + i};
        ops[i].shape_hash = ShapeHash{400 + i};
        ops[i].num_outputs = 1;
        ops[i].output_slot_ids = out;
    }

    RegionNode region{};
    init_region(&region, ops, 3);

    TensorSlot slots[1];
    auto plan = make_simple_plan(slots, 1);
    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();
    ReplayEngine engine;
    engine.init(&region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view();
    for (uint32_t i = 0; i < 3; i++) {
        auto s = engine.advance(SchemaHash{300 + i}, ShapeHash{400 + i}, av);
        assert(s == (i < 2 ? ReplayStatus::MATCH : ReplayStatus::COMPLETE));
        const auto& entry = engine.current_entry();
        assert(entry.schema_hash == SchemaHash{300 + i});
        assert(entry.shape_hash == ShapeHash{400 + i});
    }

    std::printf("  test_current_entry: PASSED\n");
}

// The plan here comes from the real sweep-line planner rather than the
// fixed-stride helper above, so the offsets are whatever the planner
// chooses and the assertions never name one.
static void test_integration_with_pool() {
    auto test = crucible::effects::testing::test();
    crucible::BackgroundThread bt;

    // Slot 2 is external: it stands for a parameter whose storage the
    // pool does not own and must not place.
    constexpr uint32_t NSLOTS = 3;
    TensorSlot slots[NSLOTS]{};
    slots[0] = {.offset_bytes = 0,
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
    slots[1] = {.offset_bytes = 0,
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
    slots[2] = {.offset_bytes = 0,
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

    auto* plan = bt.compute_memory_plan(test.alloc, slots, NSLOTS);
    assert(plan != nullptr);

    PoolAllocator pool;
    pool.init(plan);
    auto pv = pool.mint_initialized_view();

    alignas(256) char fake_param[128];
    pool.register_external(SlotId{2}, crucible::safety::NonNull<void*>{fake_param}, pv);

    SlotId op0_out[1] = {SlotId{0}};
    SlotId op0_in[1] = {SlotId{2}};
    SlotId op1_out[1] = {SlotId{1}};
    SlotId op1_in[1] = {SlotId{0}};

    TraceEntry ops[2]{};
    ops[0].schema_hash = SchemaHash{0xAAAA};
    ops[0].shape_hash = ShapeHash{0xBBBB};
    ops[0].num_outputs = 1;
    ops[0].output_slot_ids = op0_out;
    ops[0].num_inputs = 1;
    ops[0].input_slot_ids = op0_in;

    ops[1].schema_hash = SchemaHash{0xCCCC};
    ops[1].shape_hash = ShapeHash{0xDDDD};
    ops[1].num_outputs = 1;
    ops[1].output_slot_ids = op1_out;
    ops[1].num_inputs = 1;
    ops[1].input_slot_ids = op1_in;

    RegionNode region{};
    init_region(&region, ops, 2);

    ReplayEngine engine;
    engine.init(&region, ReplayEngine::PoolBorrow{pool});
    auto av = engine.mint_active_view();
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

    std::printf("  test_integration_with_pool: PASSED\n");
}

int main() {
    std::printf("test_replay_engine:\n");
    static_assert(std::is_same_v<ReplayEngine::PoolBorrow, crucible::safety::BorrowedRef<const PoolAllocator>>);
    static_assert(sizeof(ReplayEngine::PoolBorrow) == sizeof(const PoolAllocator*));
    test_linear_match();
    test_schema_divergence();
    test_shape_divergence();
    test_reset();
    test_input_ptr();
    test_invalid_slot();
    test_current_entry();
    test_integration_with_pool();
    std::printf("test_replay_engine: all tests passed\n");
    return 0;
}
