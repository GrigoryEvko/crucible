#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include <foundation/effects/Effect.h>
#include "entry_arrays.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include "tensor_data.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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

static void init_region(RegionNode* r, ::fixy::View<TraceEntry> ops, crucible::PlanLink plan) {
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
    CrucibleContext ctx{::foundation::effects::testing::test().alloc};

    CRUCIBLE_TEST_ASSERT(kTest, ctx.mode() == ContextMode::RECORD);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_recording());
    CRUCIBLE_TEST_ASSERT(kTest, !ctx.is_compiled());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.active_region().is_none());

    ::fixy::check::pass(kTest, "  test_initial_state: PASSED\n");
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
    init_region(&region, ops.view(), crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region));

    CRUCIBLE_TEST_ASSERT(kTest, ctx.mode() == ContextMode::COMPILED);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_compiled());
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(ctx.active_region(), ::fixy::some(::fixy::to(region))));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().is_initialized());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.engine().is_initialized());

    ::fixy::check::pass(kTest, "  test_activate: PASSED\n");
}

// A region with no memory plan cannot be activated, because the
// replay has no pool to hand out pointers from.
static void test_activate_no_plan() {
    ::foundation::core::FixedArray<TraceEntry, 1> ops{};

    RegionNode region{};
    init_region(&region, ops.view(), crucible::PlanLink{});

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, !ctx.activate(region));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_recording());

    ::fixy::check::pass(kTest, "  test_activate_no_plan: PASSED\n");
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
    init_region(&region, ops.view(), crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);
    auto pv = ctx.pool().mint_initialized_view(kVigilForeground);

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{100}, ShapeHash{200}, cv).status() == ReplayStatus::MATCH);
    const ::fixy::Option<::fixy::RunAddress> p0 = ctx.output_ptr(0, cv);
    CRUCIBLE_TEST_ASSERT(kTest, p0 == ctx.pool().slot_ptr(SlotId{0}, pv));

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{101}, ShapeHash{201}, cv).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 1);

    // The rewind is lazy, so after COMPLETE the position still sits on
    // the final op and its output pointer is still readable.
    const ::fixy::Option<::fixy::RunAddress> p1 = ctx.output_ptr(0, cv);
    CRUCIBLE_TEST_ASSERT(kTest, p1 == ctx.pool().slot_ptr(SlotId{1}, pv));

    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_compiled());
    // The rewind after COMPLETE puts the cursor at the first op, so the next
    // advance reads the first op of the region.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.engine().ops_matched() == 0);

    // The next advance matches the first op of the region again.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{100}, ShapeHash{200}, cv).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{101}, ShapeHash{201}, cv).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 2);

    ::fixy::check::pass(kTest, "  test_full_replay: PASSED\n");
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
    init_region(&region, ops.view(), crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{100}, ShapeHash{200}, cv).status() == ReplayStatus::MATCH);

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{999}, ShapeHash{201}, cv).status() == ReplayStatus::DIVERGED);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 1);
    // A divergence does not switch modes.  Falling back is the caller's
    // decision, which the deactivate below stands in for.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_compiled());

    ctx.deactivate();
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_recording());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.active_region().is_none());

    ::fixy::check::pass(kTest, "  test_divergence: PASSED\n");
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
    init_region(&region_a, ops_a.view(), crucible::make_plan_link(plan_a));

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
    init_region(&region_b, ops_b.view(), crucible::make_plan_link(plan_b));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};

    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region_a));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{10}, ShapeHash{20}, cv).status() == ReplayStatus::COMPLETE);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 1);

    // Activating the second region deactivates the first implicitly.
    // Each activation needs a fresh view, which is why the replays sit in
    // scopes that end before the next activation.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region_b));
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(ctx.active_region(), ::fixy::some(::fixy::to(region_b))));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{30}, ShapeHash{40}, cv).status() == ReplayStatus::MATCH);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{31}, ShapeHash{41}, cv).status() == ReplayStatus::COMPLETE);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 2);

    ::fixy::check::pass(kTest, "  test_reactivate: PASSED\n");
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
    init_region(&region, ops.view(), crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    alignas(256) char fake_param[128];
    ctx.register_external(SlotId{1}, crucible::test::host_data(fake_param), cv);

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{50}, ShapeHash{60}, cv).status() == ReplayStatus::COMPLETE);
    // The registration is checked through the pool, which is where an
    // external pointer has to land for any op to resolve it.
    auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::test::host_ptr(ctx.pool().slot_ptr(SlotId{1}, pv)) == fake_param);

    ::fixy::check::pass(kTest, "  test_external_slots: PASSED\n");
}

// The plan here comes from the real sweep-line planner rather than the
// fixed-stride helper above, so the offsets are whatever the planner
// chooses and the assertions never name one.
static void test_integration_sweep_line() {
    auto test = ::foundation::effects::testing::test();
    crucible::BackgroundThread bt{::foundation::effects::testing::test().alloc};

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
    crucible::MemoryPlan& plan = *bt.compute_memory_plan(test.alloc, plan_storage, sweep_arena, slots.view())
                                      .expect("the slots of the test fit in 64 bits");

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
    init_region(&region, ops.view(), crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    alignas(256) char fake_param[128];
    std::memset(fake_param, 0xEE, 128);
    ctx.register_external(SlotId{2}, crucible::test::host_data(fake_param), cv);
    auto pv = ctx.pool().mint_initialized_view(kVigilForeground);

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{0xAA}, ShapeHash{0xBB}, cv).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.output_ptr(0, cv) == ctx.pool().slot_ptr(SlotId{0}, pv));
    // The external slot resolves to registered storage, not into the pool.
    CRUCIBLE_TEST_ASSERT(kTest, crucible::test::host_ptr(ctx.input_ptr(0, cv)) == fake_param);

    std::memset(crucible::test::host_ptr(ctx.output_ptr(0, cv)), 0x11, 512);

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{0xCC}, ShapeHash{0xDD}, cv).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 1);

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{0xAA}, ShapeHash{0xBB}, cv).status() == ReplayStatus::MATCH);
    // Nothing is claimed about the bytes across iterations, only that the
    // address is stable: a real op would run eagerly and overwrite them.
    auto* p = crucible::test::host_ptr<uint8_t>(ctx.output_ptr(0, cv));
    CRUCIBLE_TEST_ASSERT(kTest, p == crucible::test::host_ptr(ctx.pool().slot_ptr(SlotId{0}, pv)));

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{0xCC}, ShapeHash{0xDD}, cv).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 2);

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{0xAA}, ShapeHash{0xBB}, cv).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{0xFF}, ShapeHash{0xFF}, cv).status() == ReplayStatus::DIVERGED);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 1);

    ctx.deactivate();
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_recording());

    ::fixy::check::pass(kTest, "  test_integration_sweep_line: PASSED\n");
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
    init_region(&region, ops.view(), crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};

    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{10}, ShapeHash{20}, cv).status() == ReplayStatus::MATCH);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{99}, ShapeHash{99}, cv).status() == ReplayStatus::DIVERGED);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 1);
    ctx.deactivate();

    // Each activation mints its own view, so every replay sits in a scope
    // that ends before the next activation.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{99}, ShapeHash{20}, cv).status() == ReplayStatus::DIVERGED);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 2);
    ctx.deactivate();

    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{10}, ShapeHash{20}, cv).status() == ReplayStatus::MATCH);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{11}, ShapeHash{21}, cv).status() == ReplayStatus::COMPLETE);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 2);  // unchanged
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 1);

    ::fixy::check::pass(kTest, "  test_divergence_counter: PASSED\n");
}

// The layout of a block of external bytes: 32 bytes at an alignment of 64.
static ::fixy::Layout make_external_layout() {
    return ::fixy::mint_layout(::fixy::ByteCount{32},
                               ::fixy::mint_alignment(::fixy::ByteCount{64}).expect("64 is a power of two"))
        .expect("32 bytes at an alignment of 64 is a layout");
}

// The doors of the views of an op.  input_view lends the bytes of an input
// as a read-only View, and with_output_views lends one ViewMut for each
// output.  An input that is also an output of the op, an input of an
// external slot with no View, and an input of the wrong size or alignment
// give no View.  Each refusal of the outputs gives its kind, and its body
// does not run.
static void test_slot_views() {
    using crucible::InputPosition;
    using crucible::OutputRefusal;
    ::foundation::core::FixedArray<SlotId, 1> in0{SlotId{2}};
    ::foundation::core::FixedArray<SlotId, 2> out0{SlotId{0}, SlotId{1}};
    ::foundation::core::FixedArray<SlotId, 3> in1{SlotId{0}, SlotId{4}, SlotId{1}};
    ::foundation::core::FixedArray<SlotId, 1> out1{SlotId{1}};
    ::foundation::core::FixedArray<SlotId, 1> out2{SlotId{3}};
    ::foundation::core::FixedArray<SlotId, 1> out3{SlotId{2}};
    ::foundation::core::FixedArray<SlotId, 2> out4{SlotId{0}, SlotId{0}};
    ::foundation::core::FixedArray<SlotId, 1> out5{SlotId::none()};
    ::foundation::core::FixedArray<SlotId, 9> out6{SlotId{0}, SlotId{0}, SlotId{0}, SlotId{0}, SlotId{0},
                                                   SlotId{0}, SlotId{0}, SlotId{0}, SlotId{0}};

    ::foundation::core::FixedArray<TraceEntry, 7> ops{};
    uint32_t position = 0;
    for (TraceEntry& op : ops) {
        op.schema_hash = SchemaHash{700 + position};
        op.shape_hash = ShapeHash{800 + position};
        ++position;
    }
    set_input_slots(ops.at(::foundation::core::index<0>()), in0.view());
    set_output_slots(ops.at(::foundation::core::index<0>()), out0.view());
    set_input_slots(ops.at(::foundation::core::index<1>()), in1.view());
    set_output_slots(ops.at(::foundation::core::index<1>()), out1.view());
    set_output_slots(ops.at(::foundation::core::index<2>()), out2.view());
    set_output_slots(ops.at(::foundation::core::index<3>()), out3.view());
    set_output_slots(ops.at(::foundation::core::index<4>()), out4.view());
    set_output_slots(ops.at(::foundation::core::index<5>()), out5.view());
    set_output_slots(ops.at(::foundation::core::index<6>()), out6.view());

    // Slots 0, 1 and 3 are pooled, at 0, 256 and 768.  Slot 3 holds 6
    // bytes, no whole count of floats.  Slots 2 and 4 are external.
    ::foundation::core::FixedArray<TensorSlot, 5> slots{};
    auto plan = make_simple_plan(slots.view());
    slots.at(::foundation::core::index<2>()).is_external = true;
    slots.at(::foundation::core::index<3>()).nbytes = 6;
    slots.at(::foundation::core::index<4>()).is_external = true;
    plan.num_external = 2;

    RegionNode region{};
    init_region(&region, ops.view(), crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    // Each byte of the external input holds 0x3f, so each float of it has
    // the bits 0x3f3f3f3f.  Slot 4 gets an address alone.
    ::foundation::AlignedBytes input =
        ::foundation::mint_aligned_bytes(g_test.alloc, make_external_layout()).fill(std::uint8_t{0x3f});
    ::foundation::AlignedBytes address_only =
        ::foundation::mint_aligned_bytes_zeroed(g_test.alloc, make_external_layout());
    ctx.register_external(
        SlotId{4},
        crucible::TensorData{::fixy::mint_run_address(address_only.view(), 0).expect("the block holds 32 bytes"),
                             crucible::kHostDevice},
        cv);

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{700}, ShapeHash{800}, cv).status() == ReplayStatus::MATCH);

    // An input whose first byte is off the alignment of a float, or whose
    // bytes hold no whole count of floats, gives no View of floats.
    ctx.register_external_view(SlotId{2}, input.view().window(2, 8).expect("the block holds 32 bytes"), cv);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<float>(InputPosition{0}, cv).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<std::uint8_t>(InputPosition{0}, cv)
                                    .is_some_and([](::fixy::View<std::uint8_t const> const& bytes) noexcept {
                                        return bytes.size() == 8;
                                    }));
    ctx.register_external_view(SlotId{2}, input.view().window(0, 30).expect("the block holds 32 bytes"), cv);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<float>(InputPosition{0}, cv).is_none());

    ctx.register_external_view(SlotId{2}, input.view(), cv);
    const auto is_input_value = [](::fixy::View<float const> const& values) noexcept {
        return values.size() == 8 && values.walk().all_of([](float const& value) noexcept {
            return ::fixy::bit_cast<std::uint32_t>(value) == 0x3f3f3f3fu;
        });
    };
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<float>(InputPosition{0}, cv).is_some_and(is_input_value));

    // The two outputs of op 0 get one ViewMut each, in their order.
    const auto written = ctx.with_output_views<float>(cv, [](auto& views) noexcept {
        float value = 1.5f;
        std::size_t count = 0;
        for (auto& view : views) {
            for (float& element : view) {
                element = value;
                ++count;
            }
            value += 1.0f;
        }
        return count;
    });
    CRUCIBLE_TEST_ASSERT(kTest, written.is_ok_and([](std::size_t const& count) noexcept { return count == 128; }));

    // Op 1 reads slot 0 and writes slot 1 in place.  Its input of slot 1 is
    // also its output, so it reads that slot through its ViewMut alone.  The
    // input of slot 4 has an address and no bytes.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{701}, ShapeHash{801}, cv).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(
        kTest,
        ctx.input_view<float>(InputPosition{0}, cv).is_some_and([](::fixy::View<float const> const& values) noexcept {
            return values.size() == 64 && values.walk().all_of([](float const& value) noexcept {
                return ::fixy::bit_cast<std::uint32_t>(value) == ::fixy::bit_cast<std::uint32_t>(1.5f);
            });
        }));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<float>(InputPosition{1}, cv).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<float>(InputPosition{2}, cv).is_none());
    const auto in_place = ctx.with_output_views<float>(cv, [](auto& views) noexcept {
        std::size_t matched = 0;
        for (auto& view : views) {
            for (float& element : view) {
                matched += ::fixy::bit_cast<std::uint32_t>(element) == ::fixy::bit_cast<std::uint32_t>(2.5f) ? 1u : 0u;
                element = 3.5f;
            }
        }
        return matched;
    });
    CRUCIBLE_TEST_ASSERT(kTest, in_place.is_ok_and([](std::size_t const& matched) noexcept { return matched == 64; }));

    int runs = 0;
    const auto refusal_of = [&ctx, &cv, &runs]() noexcept {
        return ctx.with_output_views<float>(cv, [&runs](auto&) noexcept { ++runs; })
            .err()
            .expect("the door refuses the outputs");
    };
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{702}, ShapeHash{802}, cv).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of() == OutputRefusal::PartialElement);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{703}, ShapeHash{803}, cv).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of() == OutputRefusal::ExternalOutput);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{704}, ShapeHash{804}, cv).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of() == OutputRefusal::Overlap);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{705}, ShapeHash{805}, cv).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of() == OutputRefusal::NoSlot);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{706}, ShapeHash{806}, cv).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of() == OutputRefusal::TooManyOutputs);
    CRUCIBLE_TEST_ASSERT(kTest, runs == 0);

    ::fixy::check::pass(kTest, "  test_slot_views: PASSED\n");
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
    test_slot_views();
    ::fixy::check::pass(kTest, "test_crucible_context: all tests passed\n");
    return 0;
}
