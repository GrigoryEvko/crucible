#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include <foundation/effects/Effect.h>
#include "entry_arrays.h"
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

// The views of the replay chain are minted on the thread that holds a
// Vigil's producer claim.  These tests run the context without a Vigil,
// so they take that context from the test door.
static constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

static void init_region(RegionNode* r, ::fixy::View<TraceEntry> ops, MemoryPlan* plan) {
    ::new(r) RegionNode{};
    r->kind = TraceNodeKind::REGION;
    r->ops = ops;
    r->plan = plan;
}

// The zero metadata and the producers of the test ops are arrays of this
// arena.  The context reads only the slot ids.
static auto g_test = ::foundation::effects::testing::test();
static ::fixy::BumpArena g_runs{size_t{1} << 16};

static void set_input_slots(TraceEntry& op, ::fixy::View<SlotId> slot_ids) {
    crucible::test::set_input_slots(g_test.alloc, g_runs, op, slot_ids);
}

static void set_output_slots(TraceEntry& op, ::fixy::View<SlotId> slot_ids) {
    crucible::test::set_output_slots(g_test.alloc, g_runs, op, slot_ids);
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

static void test_initial_state() {
    CrucibleContext ctx;

    assert(ctx.mode() == ContextMode::RECORD);
    assert(ctx.is_recording());
    assert(!ctx.is_compiled());
    assert(ctx.compiled_iterations() == 0);
    assert(ctx.diverged_count() == 0);
    assert(!ctx.active_region().has_value());

    crucible::test::pass("  test_initial_state: PASSED\n");
}

static void test_activate() {
    ::foundation::core::FixedArray<SlotId, 1> out{SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 2> ops{};
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{100};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{200};
    set_output_slots(ops.at(::foundation::core::index<0>()), out.view());
    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{101};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{201};
    set_output_slots(ops.at(::foundation::core::index<1>()), out.view());

    ::foundation::core::FixedArray<TensorSlot, 1> slots{};
    auto plan = make_simple_plan(slots.view());

    RegionNode region{};
    init_region(&region, ops.view(), &plan);

    CrucibleContext ctx;
    assert(ctx.activate(region));

    assert(ctx.mode() == ContextMode::COMPILED);
    assert(ctx.is_compiled());
    assert(ctx.active_region().try_get() == &region);
    assert(ctx.pool().is_initialized());
    assert(ctx.engine().is_initialized());

    crucible::test::pass("  test_activate: PASSED\n");
}

// A region with no memory plan cannot be activated, because the
// replay has no pool to hand out pointers from.
static void test_activate_no_plan() {
    ::foundation::core::FixedArray<TraceEntry, 1> ops{};

    RegionNode region{};
    init_region(&region, ops.view(), nullptr);

    CrucibleContext ctx;
    assert(!ctx.activate(region));
    assert(ctx.is_recording());

    crucible::test::pass("  test_activate_no_plan: PASSED\n");
}

static void test_full_replay() {
    ::foundation::core::FixedArray<SlotId, 1> out0{SlotId{0}};
    ::foundation::core::FixedArray<SlotId, 1> out1{SlotId{1}};

    ::foundation::core::FixedArray<TraceEntry, 2> ops{};
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{100};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{200};
    set_output_slots(ops.at(::foundation::core::index<0>()), out0.view());

    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{101};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{201};
    set_output_slots(ops.at(::foundation::core::index<1>()), out1.view());

    ::foundation::core::FixedArray<TensorSlot, 2> slots{};
    auto plan = make_simple_plan(slots.view());

    RegionNode region{};
    init_region(&region, ops.view(), &plan);

    CrucibleContext ctx;
    assert(ctx.activate(region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);
    auto pv = ctx.pool().mint_initialized_view(kVigilForeground);

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

    crucible::test::pass("  test_full_replay: PASSED\n");
}

static void test_divergence() {
    ::foundation::core::FixedArray<SlotId, 1> out{SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 3> ops{};
    for (uint32_t i = 0; i < 3; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{100 + i};
        op.shape_hash = ShapeHash{200 + i};
        set_output_slots(op, out.view());
    }

    ::foundation::core::FixedArray<TensorSlot, 1> slots{};
    auto plan = make_simple_plan(slots.view());

    RegionNode region{};
    init_region(&region, ops.view(), &plan);

    CrucibleContext ctx;
    assert(ctx.activate(region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    assert(ctx.advance(SchemaHash{100}, ShapeHash{200}, cv) == ReplayStatus::MATCH);

    assert(ctx.advance(SchemaHash{999}, ShapeHash{201}, cv) == ReplayStatus::DIVERGED);
    assert(ctx.diverged_count() == 1);
    // A divergence does not switch modes.  Falling back is the caller's
    // decision, which the deactivate below stands in for.
    assert(ctx.is_compiled());

    ctx.deactivate();
    assert(ctx.is_recording());
    assert(!ctx.active_region().has_value());

    crucible::test::pass("  test_divergence: PASSED\n");
}

static void test_reactivate() {
    ::foundation::core::FixedArray<SlotId, 1> out{SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 1> ops_a{};
    ops_a.at(::foundation::core::index<0>()).schema_hash = SchemaHash{10};
    ops_a.at(::foundation::core::index<0>()).shape_hash = ShapeHash{20};
    set_output_slots(ops_a.at(::foundation::core::index<0>()), out.view());

    ::foundation::core::FixedArray<TensorSlot, 1> slots_a{};
    auto plan_a = make_simple_plan(slots_a.view());

    RegionNode region_a{};
    init_region(&region_a, ops_a.view(), &plan_a);

    // The second region shares no hash with the first, so a stale guard
    // would diverge rather than silently match.
    ::foundation::core::FixedArray<SlotId, 1> out_b0{SlotId{0}};
    ::foundation::core::FixedArray<SlotId, 1> out_b1{SlotId{1}};

    ::foundation::core::FixedArray<TraceEntry, 2> ops_b{};
    ops_b.at(::foundation::core::index<0>()).schema_hash = SchemaHash{30};
    ops_b.at(::foundation::core::index<0>()).shape_hash = ShapeHash{40};
    set_output_slots(ops_b.at(::foundation::core::index<0>()), out_b0.view());
    ops_b.at(::foundation::core::index<1>()).schema_hash = SchemaHash{31};
    ops_b.at(::foundation::core::index<1>()).shape_hash = ShapeHash{41};
    set_output_slots(ops_b.at(::foundation::core::index<1>()), out_b1.view());

    ::foundation::core::FixedArray<TensorSlot, 2> slots_b{};
    auto plan_b = make_simple_plan(slots_b.view());

    RegionNode region_b{};
    init_region(&region_b, ops_b.view(), &plan_b);

    CrucibleContext ctx;

    assert(ctx.activate(region_a));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        assert(ctx.advance(SchemaHash{10}, ShapeHash{20}, cv) == ReplayStatus::COMPLETE);
    }
    assert(ctx.compiled_iterations() == 1);

    // Activating the second region deactivates the first implicitly.
    // Each activation needs a fresh view, which is why the replays sit in
    // scopes that end before the next activation.
    assert(ctx.activate(region_b));
    assert(ctx.active_region().try_get() == &region_b);
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        assert(ctx.advance(SchemaHash{30}, ShapeHash{40}, cv) == ReplayStatus::MATCH);
        assert(ctx.advance(SchemaHash{31}, ShapeHash{41}, cv) == ReplayStatus::COMPLETE);
    }
    assert(ctx.compiled_iterations() == 2);

    crucible::test::pass("  test_reactivate: PASSED\n");
}

static void test_external_slots() {
    ::foundation::core::FixedArray<SlotId, 1> out{SlotId{0}};
    ::foundation::core::FixedArray<SlotId, 1> in{SlotId{1}};

    ::foundation::core::FixedArray<TraceEntry, 1> ops{};
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{50};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{60};
    set_output_slots(ops.at(::foundation::core::index<0>()), out.view());
    set_input_slots(ops.at(::foundation::core::index<0>()), in.view());

    ::foundation::core::FixedArray<TensorSlot, 2> slots{};
    slots.at(::foundation::core::index<0>()) = {.offset_bytes = 0,
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
    slots.at(::foundation::core::index<1>()) = {.offset_bytes = 0,
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
    plan.slots = slots.view();
    plan.num_external = 1;
    plan.pool_bytes = 256;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;

    RegionNode region{};
    init_region(&region, ops.view(), &plan);

    CrucibleContext ctx;
    assert(ctx.activate(region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    alignas(256) char fake_param[128];
    ctx.register_external(SlotId{1}, ::fixy::mint_refined<::fixy::non_null, void*>(fake_param), cv);

    assert(ctx.advance(SchemaHash{50}, ShapeHash{60}, cv) == ReplayStatus::COMPLETE);
    // The registration is checked through the pool, which is where an
    // external pointer has to land for any op to resolve it.
    auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
    assert(ctx.pool().slot_ptr(SlotId{1}, pv) == fake_param);

    crucible::test::pass("  test_external_slots: PASSED\n");
}

// The plan here comes from the real sweep-line planner rather than the
// fixed-stride helper above, so the offsets are whatever the planner
// chooses and the assertions never name one.
static void test_integration_sweep_line() {
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

    ::foundation::core::FixedArray<SlotId, 1> op0_out{SlotId{0}};
    ::foundation::core::FixedArray<SlotId, 1> op0_in{SlotId{2}};
    ::foundation::core::FixedArray<SlotId, 1> op1_out{SlotId{1}};
    ::foundation::core::FixedArray<SlotId, 1> op1_in{SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 2> ops{};
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{0xAA};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{0xBB};
    set_output_slots(ops.at(::foundation::core::index<0>()), op0_out.view());
    set_input_slots(ops.at(::foundation::core::index<0>()), op0_in.view());

    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{0xCC};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{0xDD};
    set_output_slots(ops.at(::foundation::core::index<1>()), op1_out.view());
    set_input_slots(ops.at(::foundation::core::index<1>()), op1_in.view());

    RegionNode region{};
    init_region(&region, ops.view(), plan);

    CrucibleContext ctx;
    assert(ctx.activate(region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    alignas(256) char fake_param[128];
    std::memset(fake_param, 0xEE, 128);
    ctx.register_external(SlotId{2}, ::fixy::mint_refined<::fixy::non_null, void*>(fake_param), cv);
    auto pv = ctx.pool().mint_initialized_view(kVigilForeground);

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

    crucible::test::pass("  test_integration_sweep_line: PASSED\n");
}

// The divergence count belongs to the context, not to an activation, so
// it survives deactivate and re-activate.
static void test_divergence_counter() {
    ::foundation::core::FixedArray<SlotId, 1> out{SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 2> ops{};
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{10};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{20};
    set_output_slots(ops.at(::foundation::core::index<0>()), out.view());
    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{11};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{21};
    set_output_slots(ops.at(::foundation::core::index<1>()), out.view());

    ::foundation::core::FixedArray<TensorSlot, 1> slots{};
    auto plan = make_simple_plan(slots.view());

    RegionNode region{};
    init_region(&region, ops.view(), &plan);

    CrucibleContext ctx;

    assert(ctx.activate(region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        assert(ctx.advance(SchemaHash{10}, ShapeHash{20}, cv) == ReplayStatus::MATCH);
        assert(ctx.advance(SchemaHash{99}, ShapeHash{99}, cv) == ReplayStatus::DIVERGED);
    }
    assert(ctx.diverged_count() == 1);
    ctx.deactivate();

    // Each activation mints its own view, so every replay sits in a scope
    // that ends before the next activation.
    assert(ctx.activate(region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        assert(ctx.advance(SchemaHash{99}, ShapeHash{20}, cv) == ReplayStatus::DIVERGED);
    }
    assert(ctx.diverged_count() == 2);
    ctx.deactivate();

    assert(ctx.activate(region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        assert(ctx.advance(SchemaHash{10}, ShapeHash{20}, cv) == ReplayStatus::MATCH);
        assert(ctx.advance(SchemaHash{11}, ShapeHash{21}, cv) == ReplayStatus::COMPLETE);
    }
    assert(ctx.diverged_count() == 2);  // unchanged
    assert(ctx.compiled_iterations() == 1);

    crucible::test::pass("  test_divergence_counter: PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_crucible_context:\n");
    test_initial_state();
    test_activate();
    test_activate_no_plan();
    test_full_replay();
    test_divergence();
    test_reactivate();
    test_external_slots();
    test_integration_sweep_line();
    test_divergence_counter();
    crucible::test::pass("test_crucible_context: all tests passed\n");
    return 0;
}
