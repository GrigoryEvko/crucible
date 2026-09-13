#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include <crucible/effects/Capabilities.h>
#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

using crucible::CrucibleContext;
using crucible::ContextMode;
using crucible::ReplayStatus;
using crucible::RegionNode;
using crucible::TraceNodeKind;
using crucible::TraceEntry;
using crucible::TensorSlot;
using crucible::MemoryPlan;
using crucible::SlotId;
using crucible::OpIndex;
using crucible::ScalarType;
using crucible::DeviceType;
using crucible::Layout;
using crucible::SchemaHash;
using crucible::ShapeHash;

static void init_region(RegionNode* r, TraceEntry* ops, uint32_t n, MemoryPlan* plan) {
    ::new(r) RegionNode{};
    r->kind = TraceNodeKind::REGION;
    r->ops = ops;
    r->num_ops = n;
    r->plan = plan;
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

static void test_initial_state() {
    CrucibleContext ctx;

    assert(ctx.mode() == ContextMode::RECORD);
    assert(ctx.is_recording());
    assert(!ctx.is_compiled());
    assert(ctx.compiled_iterations() == 0);
    assert(ctx.diverged_count() == 0);
    assert(ctx.active_region() == nullptr);

    std::printf("  test_initial_state: PASSED\n");
}

static void test_activate() {
    SlotId out[1] = {SlotId{0}};

    TraceEntry ops[2]{};
    ops[0].schema_hash = SchemaHash{100};
    ops[0].shape_hash = ShapeHash{200};
    ops[0].num_outputs = 1;
    ops[0].output_slot_ids = out;
    ops[1].schema_hash = SchemaHash{101};
    ops[1].shape_hash = ShapeHash{201};
    ops[1].num_outputs = 1;
    ops[1].output_slot_ids = out;

    TensorSlot slots[1];
    auto plan = make_simple_plan(slots, 1);

    RegionNode region{};
    init_region(&region, ops, 2, &plan);

    CrucibleContext ctx;
    assert(ctx.activate(&region));

    assert(ctx.mode() == ContextMode::COMPILED);
    assert(ctx.is_compiled());
    assert(ctx.active_region() == &region);
    assert(ctx.pool().is_initialized());
    assert(ctx.engine().is_initialized());

    std::printf("  test_activate: PASSED\n");
}

// A region with no memory plan cannot be activated, because the
// replay has no pool to hand out pointers from.
static void test_activate_no_plan() {
    TraceEntry ops[1]{};

    RegionNode region{};
    init_region(&region, ops, 1, nullptr);

    CrucibleContext ctx;
    assert(!ctx.activate(&region));
    assert(ctx.is_recording());

    std::printf("  test_activate_no_plan: PASSED\n");
}

static void test_full_replay() {
    SlotId out0[1] = {SlotId{0}};
    SlotId out1[1] = {SlotId{1}};

    TraceEntry ops[2]{};
    ops[0].schema_hash = SchemaHash{100};
    ops[0].shape_hash = ShapeHash{200};
    ops[0].num_outputs = 1;
    ops[0].output_slot_ids = out0;
    ops[0].num_inputs = 0;

    ops[1].schema_hash = SchemaHash{101};
    ops[1].shape_hash = ShapeHash{201};
    ops[1].num_outputs = 1;
    ops[1].output_slot_ids = out1;
    ops[1].num_inputs = 0;

    TensorSlot slots[2];
    auto plan = make_simple_plan(slots, 2);

    RegionNode region{};
    init_region(&region, ops, 2, &plan);

    CrucibleContext ctx;
    assert(ctx.activate(&region));
    auto cv = ctx.mint_compiled_view();
    auto pv = ctx.pool().mint_initialized_view();

    assert(ctx.advance(SchemaHash{100}, ShapeHash{200}, cv) == ReplayStatus::MATCH);
    void* p0 = ctx.output_ptr(0, cv);
    assert(p0 == ctx.pool().slot_ptr(SlotId{0}, pv));

    assert(ctx.advance(SchemaHash{101}, ShapeHash{201}, cv) == ReplayStatus::COMPLETE);
    assert(ctx.compiled_iterations() == 1);

    // The rewind is lazy, so after COMPLETE the position still sits on
    // the final op and its output pointer is still readable.
    void* p1 = ctx.output_ptr(0, cv);
    assert(p1 == ctx.pool().slot_ptr(SlotId{1}, pv));

    assert(ctx.is_compiled());

    // The rewind happens here, on the first advance after COMPLETE.
    assert(ctx.advance(SchemaHash{100}, ShapeHash{200}, cv) == ReplayStatus::MATCH);
    assert(ctx.advance(SchemaHash{101}, ShapeHash{201}, cv) == ReplayStatus::COMPLETE);
    assert(ctx.compiled_iterations() == 2);

    std::printf("  test_full_replay: PASSED\n");
}

static void test_divergence() {
    SlotId out[1] = {SlotId{0}};

    TraceEntry ops[3]{};
    for (uint32_t i = 0; i < 3; i++) {
        ops[i].schema_hash = SchemaHash{100 + i};
        ops[i].shape_hash = ShapeHash{200 + i};
        ops[i].num_outputs = 1;
        ops[i].output_slot_ids = out;
    }

    TensorSlot slots[1];
    auto plan = make_simple_plan(slots, 1);

    RegionNode region{};
    init_region(&region, ops, 3, &plan);

    CrucibleContext ctx;
    assert(ctx.activate(&region));
    auto cv = ctx.mint_compiled_view();

    assert(ctx.advance(SchemaHash{100}, ShapeHash{200}, cv) == ReplayStatus::MATCH);

    assert(ctx.advance(SchemaHash{999}, ShapeHash{201}, cv) == ReplayStatus::DIVERGED);
    assert(ctx.diverged_count() == 1);
    // A divergence does not switch modes.  Falling back is the caller's
    // decision, which the deactivate below stands in for.
    assert(ctx.is_compiled());

    ctx.deactivate();
    assert(ctx.is_recording());
    assert(ctx.active_region() == nullptr);

    std::printf("  test_divergence: PASSED\n");
}

static void test_reactivate() {
    SlotId out[1] = {SlotId{0}};

    TraceEntry ops_a[1]{};
    ops_a[0].schema_hash = SchemaHash{10};
    ops_a[0].shape_hash = ShapeHash{20};
    ops_a[0].num_outputs = 1;
    ops_a[0].output_slot_ids = out;

    TensorSlot slots_a[1];
    auto plan_a = make_simple_plan(slots_a, 1);

    RegionNode region_a{};
    init_region(&region_a, ops_a, 1, &plan_a);

    // The second region shares no hash with the first, so a stale guard
    // would diverge rather than silently match.
    SlotId out_b[2] = {SlotId{0}, SlotId{1}};

    TraceEntry ops_b[2]{};
    ops_b[0].schema_hash = SchemaHash{30};
    ops_b[0].shape_hash = ShapeHash{40};
    ops_b[0].num_outputs = 1;
    ops_b[0].output_slot_ids = &out_b[0];
    ops_b[1].schema_hash = SchemaHash{31};
    ops_b[1].shape_hash = ShapeHash{41};
    ops_b[1].num_outputs = 1;
    ops_b[1].output_slot_ids = &out_b[1];

    TensorSlot slots_b[2];
    auto plan_b = make_simple_plan(slots_b, 2);

    RegionNode region_b{};
    init_region(&region_b, ops_b, 2, &plan_b);

    CrucibleContext ctx;

    assert(ctx.activate(&region_a));
    {
        auto cv = ctx.mint_compiled_view();
        assert(ctx.advance(SchemaHash{10}, ShapeHash{20}, cv) == ReplayStatus::COMPLETE);
    }
    assert(ctx.compiled_iterations() == 1);

    // Activating the second region deactivates the first implicitly.
    // Each activation needs a fresh view, which is why the replays sit in
    // scopes that end before the next activation.
    assert(ctx.activate(&region_b));
    assert(ctx.active_region() == &region_b);
    {
        auto cv = ctx.mint_compiled_view();
        assert(ctx.advance(SchemaHash{30}, ShapeHash{40}, cv) == ReplayStatus::MATCH);
        assert(ctx.advance(SchemaHash{31}, ShapeHash{41}, cv) == ReplayStatus::COMPLETE);
    }
    assert(ctx.compiled_iterations() == 2);

    std::printf("  test_reactivate: PASSED\n");
}

static void test_external_slots() {
    SlotId out[1] = {SlotId{0}};
    SlotId in[1] = {SlotId{1}};

    TraceEntry ops[1]{};
    ops[0].schema_hash = SchemaHash{50};
    ops[0].shape_hash = ShapeHash{60};
    ops[0].num_outputs = 1;
    ops[0].output_slot_ids = out;
    ops[0].num_inputs = 1;
    ops[0].input_slot_ids = in;

    TensorSlot slots[2]{};
    slots[0] = {.offset_bytes = 0,
                .nbytes = 256,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{1},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{0},
                .pad2 = {}};
    slots[1] = {.offset_bytes = 0,
                .nbytes = 128,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{1},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = true,
                .pad = {},
                .slot_id = SlotId{1},
                .pad2 = {}};

    MemoryPlan plan{};
    plan.slots = slots;
    plan.num_slots = 2;
    plan.num_external = 1;
    plan.pool_bytes = 256;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;

    RegionNode region{};
    init_region(&region, ops, 1, &plan);

    CrucibleContext ctx;
    assert(ctx.activate(&region));
    auto cv = ctx.mint_compiled_view();

    alignas(256) char fake_param[128];
    ctx.register_external(SlotId{1}, crucible::safety::NonNull<void*>{fake_param}, cv);

    assert(ctx.advance(SchemaHash{50}, ShapeHash{60}, cv) == ReplayStatus::COMPLETE);
    // The registration is checked through the pool, which is where an
    // external pointer has to land for any op to resolve it.
    auto pv = ctx.pool().mint_initialized_view();
    assert(ctx.pool().slot_ptr(SlotId{1}, pv) == fake_param);

    std::printf("  test_external_slots: PASSED\n");
}

// The plan here comes from the real sweep-line planner rather than the
// fixed-stride helper above, so the offsets are whatever the planner
// chooses and the assertions never name one.
static void test_integration_sweep_line() {
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

    SlotId op0_out[1] = {SlotId{0}};
    SlotId op0_in[1] = {SlotId{2}};
    SlotId op1_out[1] = {SlotId{1}};
    SlotId op1_in[1] = {SlotId{0}};

    TraceEntry ops[2]{};
    ops[0].schema_hash = SchemaHash{0xAA};
    ops[0].shape_hash = ShapeHash{0xBB};
    ops[0].num_outputs = 1;
    ops[0].output_slot_ids = op0_out;
    ops[0].num_inputs = 1;
    ops[0].input_slot_ids = op0_in;

    ops[1].schema_hash = SchemaHash{0xCC};
    ops[1].shape_hash = ShapeHash{0xDD};
    ops[1].num_outputs = 1;
    ops[1].output_slot_ids = op1_out;
    ops[1].num_inputs = 1;
    ops[1].input_slot_ids = op1_in;

    RegionNode region{};
    init_region(&region, ops, 2, plan);

    CrucibleContext ctx;
    assert(ctx.activate(&region));
    auto cv = ctx.mint_compiled_view();

    alignas(256) char fake_param[128];
    std::memset(fake_param, 0xEE, 128);
    ctx.register_external(SlotId{2}, crucible::safety::NonNull<void*>{fake_param}, cv);
    auto pv = ctx.pool().mint_initialized_view();

    assert(ctx.advance(SchemaHash{0xAA}, ShapeHash{0xBB}, cv) == ReplayStatus::MATCH);
    assert(ctx.output_ptr(0, cv) == ctx.pool().slot_ptr(SlotId{0}, pv));
    // The external slot resolves to registered storage, not into the pool.
    assert(ctx.input_ptr(0, cv) == fake_param);

    std::memset(ctx.output_ptr(0, cv), 0x11, 512);

    assert(ctx.advance(SchemaHash{0xCC}, ShapeHash{0xDD}, cv) == ReplayStatus::COMPLETE);
    assert(ctx.compiled_iterations() == 1);

    assert(ctx.advance(SchemaHash{0xAA}, ShapeHash{0xBB}, cv) == ReplayStatus::MATCH);
    // Nothing is claimed about the bytes across iterations, only that the
    // address is stable: a real op would run eagerly and overwrite them.
    auto* p = static_cast<uint8_t*>(ctx.output_ptr(0, cv));
    assert(p == ctx.pool().slot_ptr(SlotId{0}, pv));

    assert(ctx.advance(SchemaHash{0xCC}, ShapeHash{0xDD}, cv) == ReplayStatus::COMPLETE);
    assert(ctx.compiled_iterations() == 2);

    assert(ctx.advance(SchemaHash{0xAA}, ShapeHash{0xBB}, cv) == ReplayStatus::MATCH);
    assert(ctx.advance(SchemaHash{0xFF}, ShapeHash{0xFF}, cv) == ReplayStatus::DIVERGED);
    assert(ctx.diverged_count() == 1);

    ctx.deactivate();
    assert(ctx.is_recording());

    std::printf("  test_integration_sweep_line: PASSED\n");
}

// The divergence count belongs to the context, not to an activation, so
// it survives deactivate and re-activate.
static void test_divergence_counter() {
    SlotId out[1] = {SlotId{0}};

    TraceEntry ops[2]{};
    ops[0].schema_hash = SchemaHash{10};
    ops[0].shape_hash = ShapeHash{20};
    ops[0].num_outputs = 1;
    ops[0].output_slot_ids = out;
    ops[1].schema_hash = SchemaHash{11};
    ops[1].shape_hash = ShapeHash{21};
    ops[1].num_outputs = 1;
    ops[1].output_slot_ids = out;

    TensorSlot slots[1];
    auto plan = make_simple_plan(slots, 1);

    RegionNode region{};
    init_region(&region, ops, 2, &plan);

    CrucibleContext ctx;

    assert(ctx.activate(&region));
    {
        auto cv = ctx.mint_compiled_view();
        assert(ctx.advance(SchemaHash{10}, ShapeHash{20}, cv) == ReplayStatus::MATCH);
        assert(ctx.advance(SchemaHash{99}, ShapeHash{99}, cv) == ReplayStatus::DIVERGED);
    }
    assert(ctx.diverged_count() == 1);
    ctx.deactivate();

    // Each activation mints its own view, so every replay sits in a scope
    // that ends before the next activation.
    assert(ctx.activate(&region));
    {
        auto cv = ctx.mint_compiled_view();
        assert(ctx.advance(SchemaHash{99}, ShapeHash{20}, cv) == ReplayStatus::DIVERGED);
    }
    assert(ctx.diverged_count() == 2);
    ctx.deactivate();

    assert(ctx.activate(&region));
    {
        auto cv = ctx.mint_compiled_view();
        assert(ctx.advance(SchemaHash{10}, ShapeHash{20}, cv) == ReplayStatus::MATCH);
        assert(ctx.advance(SchemaHash{11}, ShapeHash{21}, cv) == ReplayStatus::COMPLETE);
    }
    assert(ctx.diverged_count() == 2);  // unchanged
    assert(ctx.compiled_iterations() == 1);

    std::printf("  test_divergence_counter: PASSED\n");
}

int main() {
    std::printf("test_crucible_context:\n");
    test_initial_state();
    test_activate();
    test_activate_no_plan();
    test_full_replay();
    test_divergence();
    test_reactivate();
    test_external_slots();
    test_integration_sweep_line();
    test_divergence_counter();
    std::printf("test_crucible_context: all tests passed\n");
    return 0;
}
