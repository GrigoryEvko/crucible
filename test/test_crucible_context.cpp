#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include <foundation/effects/Effect.h>
#include "entry_arrays.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include "tensor_data.h"
#include "test_harness.h"
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
    ctx.register_external(SlotId{1}, crucible::test::host_data(fake_param), cv)
        .expect("the plan marks slot 1 external");

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{50}, ShapeHash{60}, cv).status() == ReplayStatus::COMPLETE);
    // The registration is checked through the pool, which is where an
    // external pointer has to land for any op to resolve it.
    auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::test::host_ptr(ctx.pool().slot_ptr(SlotId{1}, pv)) == fake_param);

    ::fixy::check::pass(kTest, "  test_external_slots: PASSED\n");
}

// The layout of a block of external bytes: 64 bytes at an alignment of 64.
static ::fixy::Layout make_external_layout() {
    return ::fixy::mint_layout(::fixy::ByteCount{64},
                               ::fixy::mint_alignment(::fixy::ByteCount{64}).expect("64 is a power of two"))
        .expect("64 bytes at an alignment of 64 is a layout");
}

// A region of two ops and two slots.  Op 0 reads slot 1 and writes slot 0,
// and op 1 writes slot 0.  The plan marks slot 1 external, or puts it in the
// pool.
static crucible::DagRef<RegionNode> make_region_reading_slot_one(::fixy::BumpArena<DagArena>& arena, bool is_external,
                                                                 SchemaHash last_schema) {
    auto ops_run = arena.array(g_test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 2> const ops = ops_run.view().window<2>(0).expect("the run holds two ops");
    TraceEntry& first = ops.at(::foundation::core::index<0>());
    first.schema_hash = SchemaHash{50};
    first.shape_hash = ShapeHash{60};
    set_input_slots(first, ::fixy::check::run_of(kTest, g_test.alloc, arena, SlotId{1}));
    set_output_slots(first, ::fixy::check::run_of(kTest, g_test.alloc, arena, SlotId{0}));
    TraceEntry& last = ops.at(::foundation::core::index<1>());
    last.schema_hash = last_schema;
    last.shape_hash = ShapeHash{61};
    set_output_slots(last, ::fixy::check::run_of(kTest, g_test.alloc, arena, SlotId{0}));

    auto slots_run = arena.array(g_test.alloc, 2, TensorSlot{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TensorSlot, 2> const slots = slots_run.view().window<2>(0).expect("the run holds two slots");
    const crucible::DagRef<MemoryPlan> plan = make_simple_plan(g_test.alloc, arena, slots_run);
    slots.at(::foundation::core::index<1>()).is_external = is_external;
    plan->num_external = is_external ? 1 : 0;
    return build_region(g_test.alloc, arena, ops_run, crucible::make_plan_link(plan));
}

// The address form of the register door reads the plan of the active region.
// A slot that one region marks external is a pooled slot of another region,
// and the door refuses it there, so the slot keeps the window of the pool.
static void test_register_external_refusals() {
    using crucible::ExternalRefusal;
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const crucible::DagRef<RegionNode> external_region = make_region_reading_slot_one(arena, true, SchemaHash{51});
    const crucible::DagRef<RegionNode> pooled_region = make_region_reading_slot_one(arena, false, SchemaHash{51});
    ::foundation::AlignedBytes owner = ::foundation::mint_aligned_bytes_zeroed(g_test.alloc, make_external_layout());
    const ::fixy::RunAddress given = ::fixy::mint_run_address(owner.view(), 0).expect("the block holds 64 bytes");
    const crucible::TensorData data{given, crucible::kHostDevice};

    CrucibleContext ctx{g_test.alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*external_region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        const auto refusal_of = [&ctx, &cv, data](SlotId sid) noexcept {
            return ctx.register_external(sid, data, cv).err();
        };
        CRUCIBLE_TEST_ASSERT(kTest, refusal_of(SlotId{1}).is_none());
        CRUCIBLE_TEST_ASSERT(kTest, refusal_of(SlotId{0}) == ::fixy::some(ExternalRefusal::PooledSlot));
        CRUCIBLE_TEST_ASSERT(kTest, refusal_of(SlotId{2}) == ::fixy::some(ExternalRefusal::OutsidePlan));
        CRUCIBLE_TEST_ASSERT(kTest, refusal_of(SlotId{}) == ::fixy::some(ExternalRefusal::OutsidePlan));
        auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
        CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().slot_ptr(SlotId{1}, pv) == ::fixy::some(given));
    }

    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*pooled_region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
        const ::fixy::Option<::fixy::RunAddress> planned = ctx.pool().slot_ptr(SlotId{1}, pv);
        CRUCIBLE_TEST_ASSERT(kTest, planned.is_some() && !(planned == ::fixy::some(given)));
        CRUCIBLE_TEST_ASSERT(kTest, ctx.register_external(SlotId{1}, data, cv).err()
                                        == ::fixy::some(ExternalRefusal::PooledSlot));
        CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().slot_ptr(SlotId{1}, pv) == planned);
    }

    ::fixy::check::pass(kTest, "  test_register_external_refusals: PASSED\n");
}

// The list of the external reads belongs to the active region.  Each change
// of the active region drops it, so a bind of the next region reads no slot
// of the region before it.
static void test_external_reads_follow_the_region() {
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{8192});
    const crucible::DagRef<RegionNode> external_region = make_region_reading_slot_one(arena, true, SchemaHash{51});
    const crucible::DagRef<RegionNode> pooled_region = make_region_reading_slot_one(arena, false, SchemaHash{51});
    const crucible::DagRef<RegionNode> pooled_alternate = make_region_reading_slot_one(arena, false, SchemaHash{52});

    // Two live ops of one input, each with its own data.
    ::foundation::AlignedBytes owner = ::foundation::mint_aligned_bytes_zeroed(g_test.alloc, make_external_layout());
    const ::fixy::RunAddress first_data = ::fixy::mint_run_address(owner.view(), 0).expect("the block holds 64 bytes");
    const ::fixy::RunAddress second_data =
        ::fixy::mint_run_address(owner.view(), 32).expect("the block holds 64 bytes");
    ::fixy::FixedArray<crucible::TensorMeta, 1> first_records{};
    ::fixy::FixedArray<crucible::TensorMeta, 1> second_records{};
    first_records.update(::fixy::index<0>(), [first_data](crucible::TensorMeta& record) noexcept {
        record.data_ptr = crucible::external_data_ptr(::foundation::core::foreign_pointer(first_data));
    });
    second_records.update(::fixy::index<0>(), [second_data](crucible::TensorMeta& record) noexcept {
        record.data_ptr = crucible::external_data_ptr(::foundation::core::foreign_pointer(second_data));
    });
    crucible::TraceRing::Entry entry{};
    entry.num_inputs = 1;
    const crucible::ValidatedOp first_op = crucible::test::certify_synthetic_op(entry, first_records, 1);
    const crucible::ValidatedOp second_op = crucible::test::certify_synthetic_op(entry, second_records, 1);

    CrucibleContext ctx{g_test.alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*external_region));
    {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
        const auto held = [&ctx, &pv] noexcept { return ctx.pool().slot_ptr(SlotId{1}, pv); };
        // The activation lists no read.
        ctx.bind_external_reads(OpIndex{0}, first_op, cv);
        CRUCIBLE_TEST_ASSERT(kTest, held().is_none());
        // The list holds the one read of op 0, and a rewind gives it again.
        ctx.list_external_reads(OpIndex{0}, cv);
        ctx.bind_external_reads(OpIndex{0}, first_op, cv);
        CRUCIBLE_TEST_ASSERT(kTest, held() == ::fixy::some(first_data));
        ctx.bind_external_reads(OpIndex{1}, second_op, cv);
        CRUCIBLE_TEST_ASSERT(kTest, held() == ::fixy::some(first_data));
        ctx.rewind_external_reads();
        ctx.bind_external_reads(OpIndex{0}, second_op, cv);
        CRUCIBLE_TEST_ASSERT(kTest, held() == ::fixy::some(second_data));
        // A list that resumes at op 1 gives no read of op 0.
        ctx.list_external_reads(OpIndex{1}, cv);
        ctx.bind_external_reads(OpIndex{0}, first_op, cv);
        CRUCIBLE_TEST_ASSERT(kTest, held() == ::fixy::some(second_data));
        ctx.rewind_external_reads();
    }

    // The switch to another region after op 0 drops the list, and so does an
    // activation.  Slot 1 of the two regions is a pooled slot, and it keeps
    // its window.
    const auto check_pooled_region = [&ctx, &first_op] noexcept {
        auto cv = ctx.mint_compiled_view(kVigilForeground);
        auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
        const ::fixy::Option<::fixy::RunAddress> planned = ctx.pool().slot_ptr(SlotId{1}, pv);
        ctx.rewind_external_reads();
        ctx.bind_external_reads(OpIndex{0}, first_op, cv);
        ctx.list_external_reads(OpIndex{0}, cv);
        ctx.bind_external_reads(OpIndex{0}, first_op, cv);
        CRUCIBLE_TEST_ASSERT(kTest, planned.is_some() && ctx.pool().slot_ptr(SlotId{1}, pv) == planned);
    };
    CRUCIBLE_TEST_ASSERT(kTest, ctx.switch_region(*pooled_alternate, 1));
    check_pooled_region();

    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*external_region));
    ctx.list_external_reads(OpIndex{0}, ctx.mint_compiled_view(kVigilForeground));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*pooled_region));
    check_pooled_region();

    // A deactivation ends the pool of the region, and it drops the list with
    // it, so the next activation starts with no read of the old plan.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*external_region));
    ctx.list_external_reads(OpIndex{0}, ctx.mint_compiled_view(kVigilForeground));
    ctx.deactivate();
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*pooled_region));
    check_pooled_region();

    // The activation of the external region again starts with an empty list,
    // and a new list gives its read.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*external_region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);
    auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
    ctx.bind_external_reads(OpIndex{0}, first_op, cv);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().slot_ptr(SlotId{1}, pv).is_none());
    ctx.list_external_reads(OpIndex{0}, cv);
    ctx.bind_external_reads(OpIndex{0}, second_op, cv);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().slot_ptr(SlotId{1}, pv) == ::fixy::some(second_data));

    ::fixy::check::pass(kTest, "  test_external_reads_follow_the_region: PASSED\n");
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
    ctx.register_external(SlotId{2}, crucible::test::host_data(fake_param), cv)
        .expect("the plan marks slot 2 external");
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

// The external slot of a scope of the tests, and the first byte of it in its
// block.
static crucible::ExternalWindow make_window(SlotId slot, ::fixy::ByteCount offset,
                                            crucible::BlockPosition block = crucible::BlockPosition{0}) noexcept {
    return crucible::ExternalWindow{.offset = offset, .slot = slot, .block = block};
}

// Runs body in a scope that registers one window of the block, and gives the
// answer of the scope.  The block goes into the scope, so its name is empty in
// body, and it comes back to its name.
template <class Body>
[[nodiscard]] static auto in_block_scope(CrucibleContext& ctx, CrucibleContext::CompiledView const& cv,
                                         crucible::ExternalWindow const& window, ::foundation::AlignedBytes& block,
                                         Body&& body) noexcept {
    auto [answer, externals] = ctx.with_external_blocks(
        crucible::ExternalBlocks{.blocks = ::fixy::mint_heap_array_with<::foundation::AlignedBytes>(
                                     g_test.alloc, 1, [&block](std::size_t) noexcept { return std::move(block); }),
                                 .windows = ::fixy::mint_heap_array(g_test.alloc, 1, window)},
        cv, static_cast<Body&&>(body));
    CRUCIBLE_TEST_ASSERT(kTest, externals.blocks.size() == 1 && externals.windows.size() == 1);
    block = externals.blocks.exchange(0, ::foundation::AlignedBytes{});
    return std::move(answer);
}

// The doors of the views of an op.  with_external_blocks checks a window
// against the slot of the plan, and refuses a pooled slot, a window smaller
// than the slot and a window off the alignment of the element.  input_view
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
    // The view is const, so a body that captures it by reference writes no part
    // of it.
    const auto cv = ctx.mint_compiled_view(kVigilForeground);

    // Each byte of the external input holds 0x3f, so each float of it has
    // the bits 0x3f3f3f3f.  Slot 4 gets an address alone.
    ::foundation::AlignedBytes input =
        ::foundation::mint_aligned_bytes(g_test.alloc, make_external_layout()).fill(std::uint8_t{0x3f});
    ::foundation::AlignedBytes address_only =
        ::foundation::mint_aligned_bytes_zeroed(g_test.alloc, make_external_layout());
    ctx.register_external(
           SlotId{4},
           crucible::TensorData{::fixy::mint_run_address(address_only.view(), 0).expect("the block holds 64 bytes"),
                                crucible::kHostDevice},
           cv)
        .expect("the plan marks slot 4 external");

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{700}, ShapeHash{800}, cv).status() == ReplayStatus::MATCH);

    // The door checks a window against the slot of the plan.  A pooled slot, a
    // block with fewer bytes from the offset than the slot, an offset past the
    // block, and a first byte off the alignment of a float are refused, and the
    // slot keeps no bytes.
    const auto window_refusal_of = [&ctx, &cv, &input](SlotId sid, std::size_t offset) noexcept {
        return in_block_scope(ctx, cv, make_window(sid, ::fixy::ByteCount{offset}), input,
                              [](CrucibleContext const&) noexcept {})
            .err()
            .expect("the door refuses the window");
    };
    CRUCIBLE_TEST_ASSERT(kTest, window_refusal_of(SlotId{0}, 0) == ViewRefusal::PooledSlot);
    CRUCIBLE_TEST_ASSERT(kTest, window_refusal_of(SlotId{2}, 32) == ViewRefusal::TooSmall);
    CRUCIBLE_TEST_ASSERT(kTest, window_refusal_of(SlotId{2}, 65) == ViewRefusal::TooSmall);
    CRUCIBLE_TEST_ASSERT(kTest, window_refusal_of(SlotId{2}, ~std::size_t{0}) == ViewRefusal::TooSmall);
    CRUCIBLE_TEST_ASSERT(kTest, window_refusal_of(SlotId{2}, 2) == ViewRefusal::Misaligned);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<std::uint8_t>(InputPosition{0}, cv).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, input.size() == 64);

    // The window of nine floats at an offset of 4 bytes is off the alignment
    // of a double, and nine floats hold no whole count of doubles.  So a read
    // of doubles gives no View for each, and a read of floats gives one.  The
    // block holds more bytes than the slot, and the pool keeps the bytes of the
    // slot alone, so a read gives the nine floats of the plan.
    const auto is_input_value = [](::fixy::View<float const> const& values) noexcept {
        return values.size() == 9 && values.walk().all_of([](float const& value) noexcept {
            return ::fixy::bit_cast<std::uint32_t>(value) == 0x3f3f3f3fu;
        });
    };
    const auto reads_floats_alone = [&cv, &is_input_value](CrucibleContext const& scoped) noexcept {
        return scoped.input_view<double>(InputPosition{0}, cv).is_none()
            && scoped.input_view<float>(InputPosition{0}, cv).is_some_and(is_input_value);
    };
    const auto is_true = [](bool const& answer) noexcept { return answer; };
    for (const std::size_t offset : ::fixy::FixedArray<std::size_t, 3>{0, 4, 28}) {
        CRUCIBLE_TEST_ASSERT(
            kTest, in_block_scope(ctx, cv, make_window(SlotId{2}, ::fixy::ByteCount{offset}), input, reads_floats_alone)
                       .is_ok_and(is_true));
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<float>(InputPosition{0}, cv).is_none());

    // The two outputs of op 0 get one ViewMut each, in their order.
    const auto written = ctx.with_output_views<float>(cv, [](auto& views) noexcept {
        float value = 1.5f;
        std::size_t count = 0;
        std::move(views).drain([&value, &count](auto view) noexcept {
            for (float& element : view) {
                element = value;
                ++count;
            }
            value += 1.0f;
        });
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
        std::move(views).drain([&matched](auto view) noexcept {
            for (float& element : view) {
                matched += ::fixy::bit_cast<std::uint32_t>(element) == ::fixy::bit_cast<std::uint32_t>(2.5f) ? 1u : 0u;
                element = 3.5f;
            }
        });
        return matched;
    });
    CRUCIBLE_TEST_ASSERT(kTest, in_place.is_ok_and([](std::size_t const& matched) noexcept { return matched == 64; }));

    // The body reads the inputs of the op, whose Views stay valid while the
    // block of the pool is in the frame, and the pool holds its block again
    // after the call.
    const uint64_t pool_bytes = ctx.pool().pool_bytes();
    const auto read = ctx.with_output_views<float>(
        cv, [&ctx = ::fixy::as_const(ctx), &cv = ::fixy::as_const(cv)](auto& views) noexcept {
            return views.size() > 0 && ctx.input_view<float>(InputPosition{0}, cv).is_some();
        });
    CRUCIBLE_TEST_ASSERT(kTest, read.is_ok_and([](bool const& is_read) noexcept { return is_read; }));
    CRUCIBLE_TEST_ASSERT(kTest, pool_bytes > 0 && ctx.pool().pool_bytes() == pool_bytes);

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

// A region of one op with one output.  It writes slot 0 of the pool and reads
// the external slots 1 and 2, of 64 bytes each.
static crucible::DagRef<RegionNode> make_region_of_two_externals(::fixy::BumpArena<DagArena>& arena) {
    auto ops_run = arena.array(g_test.alloc, 1, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 1> const ops = ops_run.view().window<1>(0).expect("the run holds one op");
    TraceEntry& op = ops.at(::foundation::core::index<0>());
    op.schema_hash = SchemaHash{50};
    op.shape_hash = ShapeHash{60};
    set_input_slots(op, ::fixy::check::run_of(kTest, g_test.alloc, arena, SlotId{1}, SlotId{2}));
    set_output_slots(op, ::fixy::check::run_of(kTest, g_test.alloc, arena, SlotId{0}));

    auto slots_run = arena.array(g_test.alloc, 3, TensorSlot{});
    const crucible::DagRef<MemoryPlan> plan = make_simple_plan(g_test.alloc, arena, slots_run);
    for (TensorSlot& slot : slots_run.view()) {
        if (slot.slot_id == SlotId{0}) continue;
        slot.is_external = true;
        slot.nbytes = 64;
    }
    plan->num_external = 2;
    return build_region(g_test.alloc, arena, ops_run, crucible::make_plan_link(plan));
}

// True when the context gives the input at the position as sixteen floats.
[[nodiscard]] static bool reads_input(CrucibleContext const& ctx, CrucibleContext::CompiledView const& cv,
                                      crucible::InputPosition position) noexcept {
    return ctx.input_view<float>(position, cv).is_some_and([](::fixy::View<float const> const& values) noexcept {
        return values.size() == 16;
    });
}

// A block of 64 bytes, which is one external slot of the region.
[[nodiscard]] static ::foundation::AlignedBytes make_external_block() noexcept {
    return ::foundation::mint_aligned_bytes_zeroed(g_test.alloc, make_external_layout());
}

// A scope of external slots holds the bytes of each slot for the time of its
// body alone.  After the scope the slot has no bytes and no address, so its block
// can end.
static void test_external_blocks_live_for_their_scope() {
    using crucible::BlockPosition;
    using crucible::ExternalBlocks;
    using crucible::ExternalWindow;
    using crucible::InputPosition;
    using crucible::ViewRefusal;
    constexpr ::fixy::ByteCount no_bytes{std::size_t{0}};
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const crucible::DagRef<RegionNode> region = make_region_of_two_externals(arena);
    CrucibleContext ctx{g_test.alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    const auto cv = ctx.mint_compiled_view(kVigilForeground);
    const auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{50}, ShapeHash{60}, cv).status() == ReplayStatus::COMPLETE);
    const auto is_true = [](bool const& answer) noexcept { return answer; };
    const auto has_no_external = [&ctx, &cv, &pv]() noexcept {
        return !reads_input(ctx, cv, InputPosition{0}) && !reads_input(ctx, cv, InputPosition{1})
            && ctx.pool().slot_ptr(SlotId{1}, pv).is_none() && ctx.pool().slot_ptr(SlotId{2}, pv).is_none();
    };

    const ExternalWindow slot_one = make_window(SlotId{1}, no_bytes);
    const ExternalWindow slot_two = make_window(SlotId{2}, no_bytes);

    // The block ends after the scope, and no read finds its bytes.
    const auto reads_slot_one = [&cv](CrucibleContext const& scoped) noexcept {
        return reads_input(scoped, cv, InputPosition{0});
    };
    {
        ::foundation::AlignedBytes block = make_external_block();
        CRUCIBLE_TEST_ASSERT(kTest, in_block_scope(ctx, cv, slot_one, block, reads_slot_one).is_ok_and(is_true));
    }
    CRUCIBLE_TEST_ASSERT(kTest, has_no_external());

    // The scope owns the block, so the name of the block is empty in the body, and
    // an end of that name ends no byte of the scope.
    ::foundation::AlignedBytes first = make_external_block();
    ::foundation::AlignedBytes second = make_external_block();
    const auto ends_the_name = [&cv, &first](CrucibleContext const& scoped) noexcept {
        const bool is_empty = first.size() == 0;
        first.reset();
        return is_empty && reads_input(scoped, cv, InputPosition{0});
    };
    CRUCIBLE_TEST_ASSERT(kTest, in_block_scope(ctx, cv, slot_one, first, ends_the_name).is_ok_and(is_true));
    CRUCIBLE_TEST_ASSERT(kTest, first.size() == 64 && has_no_external());

    // A refusal in the middle of a list drops the entries before it, the body
    // does not run, and the blocks and the windows come back.
    int runs = 0;
    const auto refusal_of = [&ctx, &cv, &runs](::fixy::View<const ExternalWindow> windows) noexcept {
        auto [answer, externals] = ctx.with_external_blocks(
            ExternalBlocks{.blocks = ::fixy::mint_heap_array_with<::foundation::AlignedBytes>(
                               g_test.alloc, 2, [](std::size_t) noexcept { return make_external_block(); }),
                           .windows = ::fixy::mint_heap_array_copy<ExternalWindow>(g_test.alloc, windows)},
            cv, [&runs](CrucibleContext const&) noexcept { ++runs; });
        CRUCIBLE_TEST_ASSERT(kTest, externals.windows.size() == windows.size());
        CRUCIBLE_TEST_ASSERT(kTest, externals.blocks.size() == 2);
        for (::foundation::AlignedBytes const& block : externals.blocks) {
            CRUCIBLE_TEST_ASSERT(kTest, block.size() == 64);
        }
        return std::move(answer).err().expect("the door refuses a window");
    };
    const ::fixy::FixedArray<ExternalWindow, 3> pooled_in_the_middle{slot_one, make_window(SlotId{0}, no_bytes),
                                                                     slot_two};
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(pooled_in_the_middle.view()) == ViewRefusal::PooledSlot);
    CRUCIBLE_TEST_ASSERT(kTest, has_no_external());
    const ::fixy::FixedArray<ExternalWindow, 2> past_the_blocks{make_window(SlotId{1}, no_bytes, BlockPosition{1}),
                                                                make_window(SlotId{2}, no_bytes, BlockPosition{2})};
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(past_the_blocks.view()) == ViewRefusal::NoBlock);
    CRUCIBLE_TEST_ASSERT(kTest, has_no_external());

    // A slot has one registration at a time: an entry that names the slot of an
    // earlier entry is refused, and so is the slot of an outer scope.
    const ::fixy::FixedArray<ExternalWindow, 2> one_slot_two_times{slot_one,
                                                                   make_window(SlotId{1}, no_bytes, BlockPosition{1})};
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(one_slot_two_times.view()) == ViewRefusal::AlreadyRegistered);
    CRUCIBLE_TEST_ASSERT(kTest, has_no_external() && runs == 0);

    // Two slots can share one block.
    const ::fixy::FixedArray<ExternalWindow, 2> one_block{slot_one, slot_two};
    const auto make_block = [](std::size_t) noexcept { return make_external_block(); };
    ExternalBlocks one_block_of_two{
        .blocks = ::fixy::mint_heap_array_with<::foundation::AlignedBytes>(g_test.alloc, 1, make_block),
        .windows = ::fixy::mint_heap_array_copy<ExternalWindow>(g_test.alloc, one_block.view())};
    const auto reads_both = [&cv](CrucibleContext const& scoped) noexcept {
        return reads_input(scoped, cv, InputPosition{0}) && reads_input(scoped, cv, InputPosition{1});
    };
    auto [read_both, shared] = ctx.with_external_blocks(std::move(one_block_of_two), cv, reads_both);
    CRUCIBLE_TEST_ASSERT(kTest, read_both.is_ok_and(is_true) && shared.blocks.size() == 1 && has_no_external());

    // A scope in a body registers the other slot, and its end drops that slot
    // alone.
    const auto nests_a_scope = [&cv, &second, &slot_one, &slot_two, &reads_both,
                                &is_true](CrucibleContext& scoped) noexcept {
        const auto no_body = [](CrucibleContext const&) noexcept {};
        const bool is_refused =
            in_block_scope(scoped, cv, slot_one, second, no_body).err() == ::fixy::some(ViewRefusal::AlreadyRegistered);
        const bool is_nested = in_block_scope(scoped, cv, slot_two, second, reads_both).is_ok_and(is_true);
        return is_refused && is_nested && reads_input(scoped, cv, InputPosition{0})
            && !reads_input(scoped, cv, InputPosition{1});
    };
    CRUCIBLE_TEST_ASSERT(kTest, in_block_scope(ctx, cv, slot_one, first, nests_a_scope).is_ok_and(is_true));
    CRUCIBLE_TEST_ASSERT(kTest, has_no_external());

    // A body that meets a divergence ends early and gives it back.  The bytes are
    // gone, and the caller can deactivate.
    const auto diverges = [&cv](CrucibleContext& scoped) noexcept -> ::fixy::Option<ReplayStatus> {
        const ReplayStatus status = scoped.advance(SchemaHash{999}, ShapeHash{60}, cv).status();
        if (status == ReplayStatus::DIVERGED) return ::fixy::some(status);
        return ::fixy::none;
    };
    CRUCIBLE_TEST_ASSERT(
        kTest, in_block_scope(ctx, cv, slot_one, first, diverges)
                   .is_ok_and([](::fixy::Option<ReplayStatus> const& status) noexcept { return status.is_some(); }));
    CRUCIBLE_TEST_ASSERT(kTest, has_no_external());

    // An address that the body registers for the slot gives the slot no bytes,
    // and the end of the scope drops it.
    const ::fixy::RunAddress address = ::fixy::mint_run_address(second.view(), 0).expect("the block holds 64 bytes");
    const auto registers_an_address = [&cv, &pv, address](CrucibleContext& scoped) noexcept {
        const crucible::TensorData data{address, crucible::kHostDevice};
        return scoped.register_external(SlotId{1}, data, cv).is_ok() && !reads_input(scoped, cv, InputPosition{0})
            && scoped.pool().slot_ptr(SlotId{1}, pv) == ::fixy::some(address);
    };
    CRUCIBLE_TEST_ASSERT(kTest, in_block_scope(ctx, cv, slot_one, first, registers_an_address).is_ok_and(is_true));
    CRUCIBLE_TEST_ASSERT(kTest, has_no_external());

    // The bytes of an owner outside the program stay for the scope, with the
    // licence of the caller.
    using crucible::foreign_block_stays;
    using crucible::ForeignView;
    const ::fixy::FixedArray<ForeignView, 2> foreign{ForeignView{SlotId{1}, first.view()},
                                                     ForeignView{SlotId{2}, second.view()}};
    auto [read_foreign, views] = ctx.with_foreign_views<foreign_block_stays>(
        ::fixy::mint_heap_array_copy<ForeignView>(g_test.alloc, foreign.view()), cv,
        [&cv, &pv, address](CrucibleContext const& scoped) noexcept {
            return reads_input(scoped, cv, InputPosition{0}) && reads_input(scoped, cv, InputPosition{1})
                && scoped.pool().slot_ptr(SlotId{2}, pv) == ::fixy::some(address);
        });
    CRUCIBLE_TEST_ASSERT(kTest, read_foreign.is_ok_and(is_true) && views.size() == 2 && has_no_external());
    const ::fixy::FixedArray<ForeignView, 2> pooled_last{ForeignView{SlotId{1}, first.view()},
                                                         ForeignView{SlotId{0}, second.view()}};
    auto [refused, refused_views] = ctx.with_foreign_views<foreign_block_stays>(
        ::fixy::mint_heap_array_copy<ForeignView>(g_test.alloc, pooled_last.view()), cv,
        [&runs](CrucibleContext const&) noexcept { ++runs; });
    CRUCIBLE_TEST_ASSERT(kTest, refused.err() == ::fixy::some(ViewRefusal::PooledSlot) && refused_views.size() == 2);
    CRUCIBLE_TEST_ASSERT(kTest, has_no_external() && runs == 0);

    ctx.deactivate();
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_recording());
    ::fixy::check::pass(kTest, "  test_external_blocks_live_for_their_scope: PASSED\n");
}

// Runs change in a scope of a context that registers one external slot.
template <class Change>
static void change_in_a_block_scope(Change const& change) noexcept {
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const crucible::DagRef<RegionNode> region = make_region_of_two_externals(arena);
    CrucibleContext ctx{g_test.alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    const auto cv = ctx.mint_compiled_view(kVigilForeground);
    ::foundation::AlignedBytes block = make_external_block();
    const crucible::ExternalWindow slot_one = make_window(SlotId{1}, ::fixy::ByteCount{std::size_t{0}});
    CRUCIBLE_TEST_ASSERT(kTest, in_block_scope(ctx, cv, slot_one, block, change).is_ok());
}

// The plan of a context stays for the time of a scope of external slots: a
// change of it in the body ends the process.
static void test_external_blocks_keep_the_plan() {
    constexpr ::fixy::TextView refusal{
        "foundation: fatal: an init, a detach or the end of a PoolAllocator while a body door lends it"};
    ::fixy::check::expect_dies(
        kTest, [] noexcept { change_in_a_block_scope([](CrucibleContext& scoped) noexcept { scoped.deactivate(); }); },
        refusal);
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            change_in_a_block_scope([](CrucibleContext& scoped) noexcept {
                ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
                static_cast<void>(scoped.activate(*make_region_of_two_externals(arena)));
            });
        },
        refusal);
    change_in_a_block_scope([](CrucibleContext&) noexcept {});

    ::fixy::check::pass(kTest, "  test_external_blocks_keep_the_plan: PASSED\n");
}

// Matches the first op of a region, deactivates the context, and then reads
// through the proof of the compiled mode that the deactivation ended.
template <class Read>
static void read_through_a_stale_proof(Read const& read) noexcept {
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const crucible::DagRef<RegionNode> region = make_region_reading_slot_one(arena, false, SchemaHash{51});
    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    const auto stale = ctx.mint_compiled_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{50}, ShapeHash{60}, stale).status() == ReplayStatus::MATCH);
    ctx.deactivate();
    static_cast<void>(read(ctx, stale));
}

// deactivate takes the region and the slot table from the engine, so a read of
// the engine through a stale proof reaches no freed column of the pool.  It
// ends the process at a check of the engine.
static void test_a_stale_proof_of_the_compiled_mode_ends_the_process() {
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            read_through_a_stale_proof([](CrucibleContext const& ctx, auto const& stale) noexcept {
                return ctx.output_ptr(0, stale).is_some();
            });
        },
        ::fixy::TextView{"foundation: fatal invariant failed: is_initialized()"});
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            read_through_a_stale_proof([](CrucibleContext const& ctx, auto const& stale) noexcept {
                return ctx.input_ptr(0, stale).is_some();
            });
        },
        ::fixy::TextView{"foundation: fatal invariant failed: is_initialized()"});
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            read_through_a_stale_proof([](CrucibleContext const& ctx, auto const& stale) noexcept {
                return ctx.input_view<float>(crucible::InputPosition{0}, stale).is_some();
            });
        },
        ::fixy::TextView{"foundation: contract violation: matched.is_some()"});

    ::fixy::check::pass(kTest, "  test_a_stale_proof_of_the_compiled_mode_ends_the_process: PASSED\n");
}

// A context of static storage.  A body reaches it by a second name, which no
// type gate of a body door sees.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a static owner is the second name of a change in the body of its lend")
static CrucibleContext g_lent_context{::foundation::effects::testing::test().alloc};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// Replays the first op of a region in the static context, and lends its output
// to a body that calls change.
template <class Change>
static void change_the_lent_context(Change const& change) noexcept {
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const crucible::DagRef<RegionNode> region = make_region_reading_slot_one(arena, false, SchemaHash{51});
    CRUCIBLE_TEST_ASSERT(kTest, g_lent_context.activate(*region));
    const auto cv = g_lent_context.mint_compiled_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest,
                         g_lent_context.advance(SchemaHash{50}, ShapeHash{60}, cv).status() == ReplayStatus::MATCH);
    const auto written = g_lent_context.with_output_views<std::uint8_t>(cv, [&change](auto& views) noexcept {
        change();
        return views.size();
    });
    CRUCIBLE_TEST_ASSERT(kTest, written.is_ok_and([](std::size_t const& count) noexcept { return count == 1; }));
    g_lent_context.deactivate();
}

// The outputs of an op are lent for the time of the body, so a change of the
// plan through a second name of the context ends the process in the body.  The
// deactivation after the body is the control.
static void test_lent_outputs_refuse_a_change_of_the_plan() {
    constexpr ::fixy::TextView refusal{
        "foundation: fatal: an init, a detach or the end of a PoolAllocator while a body door lends it"};
    ::fixy::check::expect_dies(
        kTest, [] noexcept { change_the_lent_context([] noexcept { g_lent_context.deactivate(); }); }, refusal);
    ::fixy::check::expect_dies(
        kTest, [] noexcept { change_the_lent_context([] noexcept { g_lent_context.deactivate_on_divergence(); }); },
        refusal);
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            change_the_lent_context([] noexcept {
                ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
                static_cast<void>(g_lent_context.activate(*make_region_reading_slot_one(arena, false, SchemaHash{52})));
            });
        },
        refusal);
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            change_the_lent_context([] noexcept {
                ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
                static_cast<void>(
                    g_lent_context.switch_region(*make_region_reading_slot_one(arena, false, SchemaHash{52}), 1));
            });
        },
        refusal);
    change_the_lent_context([] noexcept {});
    CRUCIBLE_TEST_ASSERT(kTest, g_lent_context.is_recording());

    ::fixy::check::pass(kTest, "  test_lent_outputs_refuse_a_change_of_the_plan: PASSED\n");
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
    test_register_external_refusals();
    test_external_reads_follow_the_region();
    test_integration_sweep_line();
    test_divergence_counter();
    test_slot_views();
    test_external_blocks_live_for_their_scope();
    test_external_blocks_keep_the_plan();
    test_a_stale_proof_of_the_compiled_mode_ends_the_process();
    test_lent_outputs_refuse_a_change_of_the_plan();
    ::fixy::check::pass(kTest, "test_crucible_context: all tests passed\n");
    return 0;
}
