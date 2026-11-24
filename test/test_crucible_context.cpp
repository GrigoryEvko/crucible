#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include <foundation/effects/Effect.h>
#include "entry_arrays.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include "tensor_data.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

}  // namespace

using crucible::DagArena;
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

// A region of the ops and the plan in the arena.  The other fields of the
// region keep their defaults, so the hashes stay zero.
static crucible::DagRef<RegionNode> build_region(::foundation::effects::Alloc alloc, ::fixy::BumpArena<DagArena>& arena,
                                                 ::fixy::ArenaRun<TraceEntry const, DagArena> ops,
                                                 crucible::PlanLink plan) {
    const crucible::DagRef<RegionNode> region = crucible::mint_dag_node<RegionNode>(alloc, arena);
    region->kind = TraceNodeKind::REGION;
    region->ops = ops;
    region->plan = plan;
    return region;
}

// The zero metadata and the producers of the test ops are arrays of this
// arena.  The context reads only the slot ids.
static auto g_test = ::foundation::effects::testing::test();
static ::fixy::BumpArena<DagArena> g_runs = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});

static void set_input_slots(TraceEntry& op, ::fixy::ArenaRun<SlotId const, DagArena> slot_ids) {
    crucible::test::set_input_slots(g_test.alloc, g_runs, op, slot_ids);
}

static void set_output_slots(TraceEntry& op, ::fixy::ArenaRun<SlotId const, DagArena> slot_ids) {
    crucible::test::set_output_slots(g_test.alloc, g_runs, op, slot_ids);
}

// Every slot is internal and 256 bytes wide, so slot i sits at offset
// i * 256 and no two slots overlap.  The plan is in the arena.
static crucible::DagRef<MemoryPlan> make_simple_plan(::foundation::effects::Alloc alloc,
                                                     ::fixy::BumpArena<DagArena>& arena,
                                                     ::fixy::ArenaRun<TensorSlot, DagArena> slots) {
    const uint32_t n = static_cast<uint32_t>(slots.size());
    uint32_t i = 0;
    for (TensorSlot& slot : slots.view()) {
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
    const crucible::DagRef<MemoryPlan> plan = arena.make<MemoryPlan>(alloc);
    plan->slots = slots;
    plan->num_external = 0;
    plan->pool_bytes = n * 256;
    plan->device_type = DeviceType::CPU;
    plan->device_idx = 0;
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
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});

    auto ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 2> const ops = ops_run.view().window<2>(0).expect("the run holds two ops");
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{100};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{200};
    set_output_slots(ops.at(::foundation::core::index<0>()), out);
    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{101};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{201};
    set_output_slots(ops.at(::foundation::core::index<1>()), out);

    auto slots_run = arena.array(test.alloc, 1, TensorSlot{});
    const crucible::DagRef<MemoryPlan> plan = make_simple_plan(test.alloc, arena, slots_run);

    const crucible::DagRef<RegionNode> region =
        build_region(test.alloc, arena, ops_run, crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));

    CRUCIBLE_TEST_ASSERT(kTest, ctx.mode() == ContextMode::COMPILED);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_compiled());
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(ctx.active_region(), ::fixy::some(region)));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().is_initialized());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.engine().is_initialized());

    ::fixy::check::pass(kTest, "  test_activate: PASSED\n");
}

// A region with no memory plan cannot be activated, because the
// replay has no pool to hand out pointers from.
static void test_activate_no_plan() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    auto const ops_run = arena.array(test.alloc, 1, TraceEntry{});

    const crucible::DagRef<RegionNode> region = build_region(test.alloc, arena, ops_run, crucible::PlanLink{});

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, !ctx.activate(*region));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_recording());

    ::fixy::check::pass(kTest, "  test_activate_no_plan: PASSED\n");
}

static void test_full_replay() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out0 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});
    const auto out1 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{1});

    auto ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 2> const ops = ops_run.view().window<2>(0).expect("the run holds two ops");
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{100};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{200};
    set_output_slots(ops.at(::foundation::core::index<0>()), out0);

    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{101};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{201};
    set_output_slots(ops.at(::foundation::core::index<1>()), out1);

    auto slots_run = arena.array(test.alloc, 2, TensorSlot{});
    const crucible::DagRef<MemoryPlan> plan = make_simple_plan(test.alloc, arena, slots_run);

    const crucible::DagRef<RegionNode> region =
        build_region(test.alloc, arena, ops_run, crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
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
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});

    auto ops_run = arena.array(test.alloc, 3, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const ops = ops_run.view();
    for (uint32_t i = 0; i < 3; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{100 + i};
        op.shape_hash = ShapeHash{200 + i};
        set_output_slots(op, out);
    }

    auto slots_run = arena.array(test.alloc, 1, TensorSlot{});
    const crucible::DagRef<MemoryPlan> plan = make_simple_plan(test.alloc, arena, slots_run);

    const crucible::DagRef<RegionNode> region =
        build_region(test.alloc, arena, ops_run, crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
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
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});

    auto ops_a_run = arena.array(test.alloc, 1, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 1> const ops_a = ops_a_run.view().window<1>(0).expect("the run holds one op");
    ops_a.at(::foundation::core::index<0>()).schema_hash = SchemaHash{10};
    ops_a.at(::foundation::core::index<0>()).shape_hash = ShapeHash{20};
    set_output_slots(ops_a.at(::foundation::core::index<0>()), out);

    auto slots_a_run = arena.array(test.alloc, 1, TensorSlot{});
    const crucible::DagRef<MemoryPlan> plan_a = make_simple_plan(test.alloc, arena, slots_a_run);

    const crucible::DagRef<RegionNode> region_a =
        build_region(test.alloc, arena, ops_a_run, crucible::make_plan_link(plan_a));

    // The second region shares no hash with the first, so a stale guard
    // would diverge rather than silently match.
    const auto out_b0 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});
    const auto out_b1 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{1});

    auto ops_b_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 2> const ops_b = ops_b_run.view().window<2>(0).expect("the run holds two ops");
    ops_b.at(::foundation::core::index<0>()).schema_hash = SchemaHash{30};
    ops_b.at(::foundation::core::index<0>()).shape_hash = ShapeHash{40};
    set_output_slots(ops_b.at(::foundation::core::index<0>()), out_b0);
    ops_b.at(::foundation::core::index<1>()).schema_hash = SchemaHash{31};
    ops_b.at(::foundation::core::index<1>()).shape_hash = ShapeHash{41};
    set_output_slots(ops_b.at(::foundation::core::index<1>()), out_b1);

    auto slots_b_run = arena.array(test.alloc, 2, TensorSlot{});
    const crucible::DagRef<MemoryPlan> plan_b = make_simple_plan(test.alloc, arena, slots_b_run);

    const crucible::DagRef<RegionNode> region_b =
        build_region(test.alloc, arena, ops_b_run, crucible::make_plan_link(plan_b));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};

    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region_a));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{10}, ShapeHash{20}, cv).status() == ReplayStatus::COMPLETE);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 1);

    // Activating the second region deactivates the first implicitly.
    // Each activation needs a fresh view, which is why the replays sit in
    // scopes that end before the next activation.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region_b));
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(ctx.active_region(), ::fixy::some(region_b)));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{30}, ShapeHash{40}, cv).status() == ReplayStatus::MATCH);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{31}, ShapeHash{41}, cv).status() == ReplayStatus::COMPLETE);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 2);

    ::fixy::check::pass(kTest, "  test_reactivate: PASSED\n");
}

static void test_external_slots() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});
    const auto in = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{1});

    auto ops_run = arena.array(test.alloc, 1, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 1> const ops = ops_run.view().window<1>(0).expect("the run holds one op");
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{50};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{60};
    set_output_slots(ops.at(::foundation::core::index<0>()), out);
    set_input_slots(ops.at(::foundation::core::index<0>()), in);

    auto slots_run = arena.array(test.alloc, 2, TensorSlot{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TensorSlot, 2> const slots = slots_run.view().window<2>(0).expect("the run holds two slots");
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

    const crucible::DagRef<MemoryPlan> plan = arena.make<MemoryPlan>(test.alloc);
    plan->slots = slots_run;
    plan->num_external = 1;
    plan->pool_bytes = 256;
    plan->device_type = DeviceType::CPU;
    plan->device_idx = 0;

    const crucible::DagRef<RegionNode> region =
        build_region(test.alloc, arena, ops_run, crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    alignas(256) char fake_param[128];
    ctx.register_external(
        crucible::mint_external_slot(plan->slots.view(), SlotId{1}).expect("the plan marks slot 1 external"),
        crucible::test::host_data(fake_param), cv);

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
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});

    // Slot 2 is external: it stands for a parameter whose storage the
    // pool does not own and must not place.
    constexpr uint32_t NSLOTS = 3;
    auto slots_run = arena.array(test.alloc, NSLOTS, TensorSlot{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TensorSlot, NSLOTS> const slots =
        slots_run.view().window<NSLOTS>(0).expect("the run holds three slots");
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

    // The sweep writes the plan into the arena of the slots.
    const crucible::DagRef<crucible::MemoryPlan> plan_storage = arena.make<crucible::MemoryPlan>(test.alloc);
    crucible::PlanScratch sweep_scratch{Bytes{size_t{1} << 16}};
    crucible::BackgroundThread::PlanTarget const target{};
    sweep_scratch.arena.with_epoch([target, alloc = test.alloc, plan_storage,
                                    &slots_run](crucible::PlanScratch::Epoch& epoch) noexcept {
        static_cast<void>(crucible::BackgroundThread::compute_memory_plan(alloc, target, plan_storage, epoch, slots_run)
                              .expect("the slots of the test fit in 64 bits"));
    });

    const auto op0_out = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});
    const auto op0_in = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{2});
    const auto op1_out = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{1});
    const auto op1_in = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});

    auto ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 2> const ops = ops_run.view().window<2>(0).expect("the run holds two ops");
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{0xAA};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{0xBB};
    set_output_slots(ops.at(::foundation::core::index<0>()), op0_out);
    set_input_slots(ops.at(::foundation::core::index<0>()), op0_in);

    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{0xCC};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{0xDD};
    set_output_slots(ops.at(::foundation::core::index<1>()), op1_out);
    set_input_slots(ops.at(::foundation::core::index<1>()), op1_in);

    const crucible::DagRef<RegionNode> region =
        build_region(test.alloc, arena, ops_run, crucible::make_plan_link(plan_storage));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    alignas(256) char fake_param[128];
    std::memset(fake_param, 0xEE, 128);
    ctx.register_external(
        crucible::mint_external_slot(plan_storage->slots.view(), SlotId{2}).expect("the plan marks slot 2 external"),
        crucible::test::host_data(fake_param), cv);
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
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});

    auto ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 2> const ops = ops_run.view().window<2>(0).expect("the run holds two ops");
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{10};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{20};
    set_output_slots(ops.at(::foundation::core::index<0>()), out);
    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{11};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{21};
    set_output_slots(ops.at(::foundation::core::index<1>()), out);

    auto slots_run = arena.array(test.alloc, 1, TensorSlot{});
    const crucible::DagRef<MemoryPlan> plan = make_simple_plan(test.alloc, arena, slots_run);

    const crucible::DagRef<RegionNode> region =
        build_region(test.alloc, arena, ops_run, crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};

    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{10}, ShapeHash{20}, cv).status() == ReplayStatus::MATCH);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{99}, ShapeHash{99}, cv).status() == ReplayStatus::DIVERGED);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 1);
    ctx.deactivate();

    // Each activation mints its own view, so every replay sits in a scope
    // that ends before the next activation.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{99}, ShapeHash{20}, cv).status() == ReplayStatus::DIVERGED);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 2);
    ctx.deactivate();

    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{10}, ShapeHash{20}, cv).status() == ReplayStatus::MATCH);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{11}, ShapeHash{21}, cv).status() == ReplayStatus::COMPLETE);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 2);  // unchanged
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 1);

    ::fixy::check::pass(kTest, "  test_divergence_counter: PASSED\n");
}

// The layout of a block of external bytes: 64 bytes at an alignment of 64.
static ::fixy::Layout make_external_layout() {
    return ::fixy::mint_layout(::fixy::ByteCount{64},
                               ::fixy::mint_alignment(::fixy::ByteCount{64}).expect("64 is a power of two"))
        .expect("64 bytes at an alignment of 64 is a layout");
}

// The doors of the views of an op.  register_external_view checks a View
// against the slot of the plan, and refuses a pooled slot, a View smaller
// than the slot and a View off the alignment of the element.  input_view
// lends the bytes of an input as a read-only View, and with_output_views
// lends one ViewMut for each output.  An input that is also an output of the
// op, an input of an external slot with no View, and an input of the wrong
// size or alignment for the type of the read give no View.  Each refusal of
// the outputs gives its kind, and its body does not run.
static void test_slot_views() {
    using crucible::InputPosition;
    using crucible::OutputRefusal;
    using crucible::ViewRefusal;
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto in0 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{2});
    const auto out0 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0}, SlotId{1});
    const auto in1 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0}, SlotId{4}, SlotId{1});
    const auto out1 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{1});
    const auto out2 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{3});
    const auto out3 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{2});
    const auto out4 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0}, SlotId{0});
    const auto out5 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId::none());
    auto const out6_run = arena.array(test.alloc, 9, SlotId{0});

    auto ops_run = arena.array(test.alloc, 7, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 7> const ops = ops_run.view().window<7>(0).expect("the run holds seven ops");
    uint32_t position = 0;
    for (TraceEntry& op : ops) {
        op.schema_hash = SchemaHash{700 + position};
        op.shape_hash = ShapeHash{800 + position};
        ++position;
    }
    set_input_slots(ops.at(::foundation::core::index<0>()), in0);
    set_output_slots(ops.at(::foundation::core::index<0>()), out0);
    set_input_slots(ops.at(::foundation::core::index<1>()), in1);
    set_output_slots(ops.at(::foundation::core::index<1>()), out1);
    set_output_slots(ops.at(::foundation::core::index<2>()), out2);
    set_output_slots(ops.at(::foundation::core::index<3>()), out3);
    set_output_slots(ops.at(::foundation::core::index<4>()), out4);
    set_output_slots(ops.at(::foundation::core::index<5>()), out5);
    set_output_slots(ops.at(::foundation::core::index<6>()), out6_run);

    // Slots 0, 1 and 3 are pooled, at 0, 256 and 768.  Slot 3 holds 6
    // bytes, no whole count of floats.  Slots 2 and 4 are external, and
    // slot 2 holds 36 bytes, nine floats.
    auto slots_run = arena.array(test.alloc, 5, TensorSlot{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TensorSlot, 5> const slots = slots_run.view().window<5>(0).expect("the run holds five slots");
    const crucible::DagRef<MemoryPlan> plan = make_simple_plan(test.alloc, arena, slots_run);
    slots.at(::foundation::core::index<2>()).is_external = true;
    slots.at(::foundation::core::index<2>()).nbytes = 36;
    slots.at(::foundation::core::index<3>()).nbytes = 6;
    slots.at(::foundation::core::index<4>()).is_external = true;
    plan->num_external = 2;

    const crucible::DagRef<RegionNode> region =
        build_region(test.alloc, arena, ops_run, crucible::make_plan_link(plan));

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    // Each byte of the external input holds 0x3f, so each float of it has
    // the bits 0x3f3f3f3f.  Slot 4 gets an address alone.
    ::foundation::AlignedBytes input =
        ::foundation::mint_aligned_bytes(g_test.alloc, make_external_layout()).fill(std::uint8_t{0x3f});
    ::foundation::AlignedBytes address_only =
        ::foundation::mint_aligned_bytes_zeroed(g_test.alloc, make_external_layout());
    ctx.register_external(
        crucible::mint_external_slot(plan->slots.view(), SlotId{4}).expect("the plan marks slot 4 external"),
        crucible::TensorData{::fixy::mint_run_address(address_only.view(), 0).expect("the block holds 64 bytes"),
                             crucible::kHostDevice},
        cv);

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{700}, ShapeHash{800}, cv).status() == ReplayStatus::MATCH);

    // The door checks a View against the slot of the plan.  A pooled slot,
    // a View of fewer bytes than the slot, and a View off the alignment of a
    // float are refused, and the slot keeps no View.
    const ::fixy::View<std::uint8_t> bytes = input.view();
    const auto view_refusal_of = [&ctx, &cv](SlotId sid, ::fixy::View<std::uint8_t> given) noexcept {
        return ctx.register_external_view(sid, given, cv).err().expect("the door refuses the View");
    };
    CRUCIBLE_TEST_ASSERT(kTest, view_refusal_of(SlotId{0}, bytes.window(0, 36).expect("the block holds 64 bytes"))
                                    == ViewRefusal::PooledSlot);
    CRUCIBLE_TEST_ASSERT(kTest, view_refusal_of(SlotId{2}, bytes.window(0, 32).expect("the block holds 64 bytes"))
                                    == ViewRefusal::TooSmall);
    CRUCIBLE_TEST_ASSERT(kTest, view_refusal_of(SlotId{2}, bytes.window(2, 36).expect("the block holds 64 bytes"))
                                    == ViewRefusal::Misaligned);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<std::uint8_t>(InputPosition{0}, cv).is_none());

    // The View of nine floats at an offset of 4 bytes is off the alignment
    // of a double, and nine floats hold no whole count of doubles.  So a read
    // of doubles gives no View for each, and a read of floats gives one.  A
    // View of 64 bytes holds more bytes than the slot, and the pool keeps
    // its first 36, so a read gives the nine floats of the plan.
    const auto is_input_value = [](::fixy::View<float const> const& values) noexcept {
        return values.size() == 9 && values.walk().all_of([](float const& value) noexcept {
            return ::fixy::bit_cast<std::uint32_t>(value) == 0x3f3f3f3fu;
        });
    };
    ctx.register_external_view(SlotId{2}, bytes.window(4, 36).expect("the block holds 64 bytes"), cv)
        .expect("the View holds the 36 bytes of slot 2");
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<double>(InputPosition{0}, cv).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<float>(InputPosition{0}, cv).is_some_and(is_input_value));
    ctx.register_external_view(SlotId{2}, bytes.window(0, 36).expect("the block holds 64 bytes"), cv)
        .expect("the View holds the 36 bytes of slot 2");
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<double>(InputPosition{0}, cv).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<float>(InputPosition{0}, cv).is_some_and(is_input_value));
    ctx.register_external_view(SlotId{2}, bytes, cv).expect("the View holds more than the 36 bytes of slot 2");
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
