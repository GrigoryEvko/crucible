#include <crucible/CrucibleContext.h>
#include <crucible/ReplayEngine.h>
#include <crucible/BackgroundThread.h>
#include <foundation/effects/Effect.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include "entry_arrays.h"
#include <fixy/Core.h>
#include <fixy/check/Fixture.h>
#include "tensor_data.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

}  // namespace

using crucible::DagArena;

// The views of the replay chain are minted on the thread that holds a
// Vigil's producer claim.  These tests run the chain without a Vigil, so
// they take that context from the test door.
static constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

using crucible::CrucibleContext;
using crucible::ReplayEngine;
using crucible::ReplayStatus;
using crucible::ReplayStep;
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
// rest are left at their defaults on purpose.  The region comes fresh from
// mint_dag_node in a frame on the arena, so each other field of the region
// holds its default.
static crucible::DagRef<RegionNode> build_region(::foundation::effects::Alloc alloc, ::fixy::BumpArena<DagArena>& arena,
                                                 ::fixy::ArenaRun<TraceEntry const, DagArena> ops) {
    return ::foundation::core::with_arena_frame(arena, [alloc, &ops](auto& frame) noexcept {
        const auto region = crucible::mint_dag_node<RegionNode>(alloc, frame);
        region->kind = TraceNodeKind::REGION;
        region->ops = ops;
        return region.weaken();
    });
}

// The zero metadata and the producers of the test ops are arrays of this
// arena.  The replay engine reads only the slot ids.
static auto g_test = ::foundation::effects::testing::test();
static ::fixy::BumpArena<DagArena> g_runs = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});

static void set_input_slots(TraceEntry& op, ::fixy::ArenaRun<SlotId const, DagArena> slot_ids) {
    crucible::test::set_input_slots(g_test.alloc, g_runs, op, slot_ids);
}

static void set_output_slots(TraceEntry& op, ::fixy::ArenaRun<SlotId const, DagArena> slot_ids) {
    crucible::test::set_output_slots(g_test.alloc, g_runs, op, slot_ids);
}

// Every slot is internal and 256 bytes wide, so slot i sits at offset
// i * 256 and no two slots overlap.  The plan is in the arena of the slots,
// and a context that replays its region owns the pool of the plan.
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

static void test_linear_match() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});

    auto ops_run = arena.array(test.alloc, 3, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const ops = ops_run.view();
    for (uint32_t i = 0; i < 3; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{100 + i};
        op.shape_hash = ShapeHash{200 + i};
        set_output_slots(op, ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{i}));
    }

    const crucible::DagRef<RegionNode> region = build_region(test.alloc, arena, ops_run);

    auto slots_run = arena.array(test.alloc, 3, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(test.alloc, arena, slots_run));
    CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    const PoolAllocator& pool = ctx.pool();
    auto pv = pool.mint_initialized_view(kVigilForeground);

    ReplayEngine& engine = ctx.engine(test);
    auto av = engine.mint_active_view(kVigilForeground);

    CRUCIBLE_TEST_ASSERT(kTest, engine.is_initialized());
    CRUCIBLE_TEST_ASSERT(kTest, engine.num_ops() == 3);
    CRUCIBLE_TEST_ASSERT(kTest, engine.ops_matched() == 0);

    // Every op but the last reports MATCH and the last reports COMPLETE,
    // which is why the final step is unrolled out of the loop.
    for (uint32_t i = 0; i < 2; i++) {
        const ReplayStep step = engine.advance(SchemaHash{100 + i}, ShapeHash{200 + i}, av);
        CRUCIBLE_TEST_ASSERT(kTest, step.status() == ReplayStatus::MATCH);
        CRUCIBLE_TEST_ASSERT(kTest, step.matched_op_index().value() == OpIndex{i});
        CRUCIBLE_TEST_ASSERT(kTest, engine.ops_matched() == i + 1);
        CRUCIBLE_TEST_ASSERT(kTest, engine.matched_op_index().value() == OpIndex{i});

        const ::fixy::Option<::fixy::RunAddress> ptr = engine.output_ptr(0, av);
        CRUCIBLE_TEST_ASSERT(kTest, ptr == pool.slot_ptr(SlotId{i}, pv));
    }

    {
        const ReplayStep step = engine.advance(SchemaHash{102}, ShapeHash{202}, av);
        CRUCIBLE_TEST_ASSERT(kTest, step.status() == ReplayStatus::COMPLETE);
        CRUCIBLE_TEST_ASSERT(kTest, step.matched_op_index().value() == OpIndex{2});
        // The advance that completes the region takes the cursor back to the
        // first op, and the matched op stays the last op.
        CRUCIBLE_TEST_ASSERT(kTest, engine.ops_matched() == 0);
        CRUCIBLE_TEST_ASSERT(kTest, engine.matched_op_index().value() == OpIndex{2});
        const ::fixy::Option<::fixy::RunAddress> ptr = engine.output_ptr(0, av);
        CRUCIBLE_TEST_ASSERT(kTest, ptr == pool.slot_ptr(SlotId{2}, pv));
    }
    // The next iteration starts at the first op with no reset.
    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{100}, ShapeHash{200}, av).status() == ReplayStatus::MATCH);

    ::fixy::check::pass(kTest, "  test_linear_match: PASSED\n");
}

static void test_schema_divergence() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out_slot = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});

    auto ops_run = arena.array(test.alloc, 3, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const ops = ops_run.view();
    for (uint32_t i = 0; i < 3; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{100 + i};
        op.shape_hash = ShapeHash{200};
        set_output_slots(op, out_slot);
    }

    const crucible::DagRef<RegionNode> region = build_region(test.alloc, arena, ops_run);

    auto slots_run = arena.array(test.alloc, 1, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(test.alloc, arena, slots_run));
    CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    const PoolAllocator& pool = ctx.pool();
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine& engine = ctx.engine(test);
    auto av = engine.mint_active_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{100}, ShapeHash{200}, av).status() == ReplayStatus::MATCH);

    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{999}, ShapeHash{200}, av).status() == ReplayStatus::DIVERGED);
    CRUCIBLE_TEST_ASSERT(kTest, engine.diverged_op_index_tagged().value() == OpIndex{1});
    CRUCIBLE_TEST_ASSERT(kTest, engine.ops_matched() == 1);

    // The position stays on the diverged op, so a second advance
    // diverges again instead of stepping over it.
    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{999}, ShapeHash{200}, av).status() == ReplayStatus::DIVERGED);
    CRUCIBLE_TEST_ASSERT(kTest, engine.diverged_op_index_tagged().value() == OpIndex{1});

    ::fixy::check::pass(kTest, "  test_schema_divergence: PASSED\n");
}

static void test_shape_divergence() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out_slot = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});

    auto ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 2> const ops = ops_run.view().window<2>(0).expect("the run holds two ops");
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{100};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{200};
    set_output_slots(ops.at(::foundation::core::index<0>()), out_slot);
    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{101};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{201};
    set_output_slots(ops.at(::foundation::core::index<1>()), out_slot);

    const crucible::DagRef<RegionNode> region = build_region(test.alloc, arena, ops_run);

    auto slots_run = arena.array(test.alloc, 1, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(test.alloc, arena, slots_run));
    CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    const PoolAllocator& pool = ctx.pool();
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine& engine = ctx.engine(test);
    auto av = engine.mint_active_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{100}, ShapeHash{200}, av).status() == ReplayStatus::MATCH);

    // The schema still matches here, so only the shape can be the cause
    // of the divergence.
    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{101}, ShapeHash{999}, av).status() == ReplayStatus::DIVERGED);
    CRUCIBLE_TEST_ASSERT(kTest, engine.diverged_op_index_tagged().value() == OpIndex{1});

    ::fixy::check::pass(kTest, "  test_shape_divergence: PASSED\n");
}

static void test_reset() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out_slot = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});

    auto ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 2> const ops = ops_run.view().window<2>(0).expect("the run holds two ops");
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{10};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{20};
    set_output_slots(ops.at(::foundation::core::index<0>()), out_slot);
    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{11};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{21};
    set_output_slots(ops.at(::foundation::core::index<1>()), out_slot);

    const crucible::DagRef<RegionNode> region = build_region(test.alloc, arena, ops_run);

    auto slots_run = arena.array(test.alloc, 1, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(test.alloc, arena, slots_run));
    CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    const PoolAllocator& pool = ctx.pool();
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine& engine = ctx.engine(test);
    auto av = engine.mint_active_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{10}, ShapeHash{20}, av).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{11}, ShapeHash{21}, av).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_TEST_ASSERT(kTest, engine.ops_matched() == 0);

    // A reset at the first op keeps the matched op, the last op of the region.
    engine.reset(av);
    CRUCIBLE_TEST_ASSERT(kTest, engine.ops_matched() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, engine.matched_op_index().value() == OpIndex{1});

    // A reset in the middle of the region abandons the iteration.
    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{10}, ShapeHash{20}, av).status() == ReplayStatus::MATCH);
    engine.reset(av);
    CRUCIBLE_TEST_ASSERT(kTest, engine.ops_matched() == 0);

    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{10}, ShapeHash{20}, av).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{11}, ShapeHash{21}, av).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_TEST_ASSERT(kTest, engine.ops_matched() == 0);

    ::fixy::check::pass(kTest, "  test_reset: PASSED\n");
}

// Op 1 reads the slot op 0 writes, so the two ops alias one pool
// region through different entries.
static void test_input_ptr() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out0 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});
    const auto out1 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{1});
    const auto in1 = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});

    auto ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 2> const ops = ops_run.view().window<2>(0).expect("the run holds two ops");
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{50};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{60};
    set_output_slots(ops.at(::foundation::core::index<0>()), out0);

    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{51};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{61};
    set_output_slots(ops.at(::foundation::core::index<1>()), out1);
    set_input_slots(ops.at(::foundation::core::index<1>()), in1);

    const crucible::DagRef<RegionNode> region = build_region(test.alloc, arena, ops_run);

    auto slots_run = arena.array(test.alloc, 2, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(test.alloc, arena, slots_run));
    CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    const PoolAllocator& pool = ctx.pool();
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine& engine = ctx.engine(test);
    auto av = engine.mint_active_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{50}, ShapeHash{60}, av).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, engine.output_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));

    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{51}, ShapeHash{61}, av).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_TEST_ASSERT(kTest, engine.output_ptr(0, av) == pool.slot_ptr(SlotId{1}, pv));
    CRUCIBLE_TEST_ASSERT(kTest, engine.input_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));

    CRUCIBLE_TEST_ASSERT(kTest, engine.input_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));

    ::fixy::check::pass(kTest, "  test_input_ptr: PASSED\n");
}

static void test_invalid_slot() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0}, SlotId::none());

    auto ops_run = arena.array(test.alloc, 1, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 1> const ops = ops_run.view().window<1>(0).expect("the run holds one op");
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{77};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{88};
    set_output_slots(ops.at(::foundation::core::index<0>()), out);

    const crucible::DagRef<RegionNode> region = build_region(test.alloc, arena, ops_run);

    auto slots_run = arena.array(test.alloc, 1, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(test.alloc, arena, slots_run));
    CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    const PoolAllocator& pool = ctx.pool();
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine& engine = ctx.engine(test);
    auto av = engine.mint_active_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest, engine.advance(SchemaHash{77}, ShapeHash{88}, av).status() == ReplayStatus::COMPLETE);

    CRUCIBLE_TEST_ASSERT(kTest, engine.output_ptr(0, av).is_some());
    CRUCIBLE_TEST_ASSERT(kTest, engine.output_ptr(1, av).is_none());

    ::fixy::check::pass(kTest, "  test_invalid_slot: PASSED\n");
}

static void test_current_entry() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});

    auto ops_run = arena.array(test.alloc, 3, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const ops = ops_run.view();
    for (uint32_t i = 0; i < 3; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{300 + i};
        op.shape_hash = ShapeHash{400 + i};
        set_output_slots(op, out);
    }

    const crucible::DagRef<RegionNode> region = build_region(test.alloc, arena, ops_run);

    auto slots_run = arena.array(test.alloc, 1, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(test.alloc, arena, slots_run));
    CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    const PoolAllocator& pool = ctx.pool();
    auto pv = pool.mint_initialized_view(kVigilForeground);
    ReplayEngine& engine = ctx.engine(test);
    auto av = engine.mint_active_view(kVigilForeground);
    for (uint32_t i = 0; i < 3; i++) {
        const ReplayStep step = engine.advance(SchemaHash{300 + i}, ShapeHash{400 + i}, av);
        CRUCIBLE_TEST_ASSERT(kTest, step.status() == (i < 2 ? ReplayStatus::MATCH : ReplayStatus::COMPLETE));
        const auto& entry = engine.current_entry();
        CRUCIBLE_TEST_ASSERT(kTest, entry.schema_hash == SchemaHash{300 + i});
        CRUCIBLE_TEST_ASSERT(kTest, entry.shape_hash == ShapeHash{400 + i});
    }

    ::fixy::check::pass(kTest, "  test_current_entry: PASSED\n");
}

// The reset at the end of an iteration keeps the matched op, so the matched
// index stays on the last op while the position goes back to the first.
static void test_matched_index_after_reset() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out_slot = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});
    auto ops_run = arena.array(test.alloc, 3, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const ops = ops_run.view();
    for (uint32_t i = 0; i < 3; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{500 + i};
        op.shape_hash = ShapeHash{600 + i};
        set_output_slots(op, out_slot);
    }
    const crucible::DagRef<RegionNode> region = build_region(test.alloc, arena, ops_run);
    auto slots_run = arena.array(test.alloc, 1, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(test.alloc, arena, slots_run));
    CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    ReplayEngine& engine = ctx.engine(test);
    auto av = engine.mint_active_view(kVigilForeground);
    for (uint32_t i = 0; i < 3; i++) {
        static_cast<void>(engine.advance(SchemaHash{500 + i}, ShapeHash{600 + i}, av));
    }
    CRUCIBLE_FATAL_INVARIANT(engine.ops_matched() == 0);
    engine.reset(av);
    CRUCIBLE_FATAL_INVARIANT(engine.ops_matched() == 0);
    CRUCIBLE_FATAL_INVARIANT(engine.matched_op_index().value() == OpIndex{2});
    CRUCIBLE_FATAL_INVARIANT(engine.current_entry().schema_hash == SchemaHash{502});
    CRUCIBLE_FATAL_INVARIANT(engine.diverged_op_index_tagged().value() == OpIndex{0});

    ::fixy::check::pass(kTest, "  test_matched_index_after_reset: PASSED\n");
}

// The matched index and the matched entry of a three-op region name one op.
// Each op has its own schema hash, so the entry names its own position.
static void require_matched(const ReplayEngine& engine, uint32_t position) {
    CRUCIBLE_FATAL_INVARIANT(engine.matched_op_index().value() == OpIndex{position});
    CRUCIBLE_FATAL_INVARIANT(engine.current_entry().schema_hash == SchemaHash{700 + position});
}

// The engine reads the matched index from its cursor.  The matched op is the
// op before the cursor, or the last op after the rewind at the end of an
// iteration.  Each status keeps the two in step: a MATCH, a COMPLETE, the
// rewind, a divergence at the first op after the rewind, and a divergence
// after a MATCH.  The step of each MATCH and each COMPLETE gives the same
// position as the engine.
static void test_matched_index_follows_the_cursor() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out_slot = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0});
    auto ops_run = arena.array(test.alloc, 3, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const ops = ops_run.view();
    for (uint32_t i = 0; i < 3; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{700 + i};
        op.shape_hash = ShapeHash{800 + i};
        set_output_slots(op, out_slot);
    }
    const crucible::DagRef<RegionNode> region = build_region(test.alloc, arena, ops_run);
    auto slots_run = arena.array(test.alloc, 1, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(test.alloc, arena, slots_run));
    CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    ReplayEngine& engine = ctx.engine(test);
    auto av = engine.mint_active_view(kVigilForeground);
    for (uint32_t round = 0; round < 2; round++) {
        for (uint32_t i = 0; i < 3; i++) {
            const ReplayStep step = engine.advance(SchemaHash{700 + i}, ShapeHash{800 + i}, av);
            CRUCIBLE_FATAL_INVARIANT(step.status() == (i < 2 ? ReplayStatus::MATCH : ReplayStatus::COMPLETE));
            CRUCIBLE_FATAL_INVARIANT(step.matched_op_index().value() == OpIndex{i});
            require_matched(engine, i);
        }
        engine.reset(av);
        require_matched(engine, 2);
    }
    CRUCIBLE_FATAL_INVARIANT(engine.advance(SchemaHash{999}, ShapeHash{800}, av).status() == ReplayStatus::DIVERGED);
    require_matched(engine, 2);
    CRUCIBLE_FATAL_INVARIANT(engine.advance(SchemaHash{700}, ShapeHash{800}, av).status() == ReplayStatus::MATCH);
    CRUCIBLE_FATAL_INVARIANT(engine.advance(SchemaHash{999}, ShapeHash{801}, av).status() == ReplayStatus::DIVERGED);
    require_matched(engine, 0);

    ::fixy::check::pass(kTest, "  test_matched_index_follows_the_cursor: PASSED\n");
}

// A rewind from the middle of a region abandons the iteration, so no op stays
// matched, and each read of the matched op ends the process.  The next
// iteration matches from the first op.
static void test_rewind_in_the_middle_drops_the_matched_op(::fixy::TestRunnerCtx const& test) {
    auto source = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out_slot = ::fixy::check::run_of(kTest, source.alloc, arena, SlotId{0});
    auto ops_run = arena.array(source.alloc, 3, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const ops = ops_run.view();
    for (uint32_t i = 0; i < 3; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{700 + i};
        op.shape_hash = ShapeHash{800 + i};
        set_output_slots(op, out_slot);
    }
    const crucible::DagRef<RegionNode> region = build_region(source.alloc, arena, ops_run);
    auto slots_run = arena.array(source.alloc, 1, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(source.alloc, arena, slots_run));
    CrucibleContext ctx{source.alloc};
    auto context_key = crucible::mint_context_key(source);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    ReplayEngine& engine = ctx.engine(source);
    auto av = engine.mint_active_view(kVigilForeground);
    CRUCIBLE_FATAL_INVARIANT(engine.advance(SchemaHash{700}, ShapeHash{800}, av).status() == ReplayStatus::MATCH);
    engine.reset(av);
    CRUCIBLE_FATAL_INVARIANT(engine.ops_matched() == 0);
    ::fixy::check::expect_dies(
        test, [&engine] noexcept { static_cast<void>(engine.matched_op_index()); },
        ::fixy::TextView{"contract violation: matched.is_some()"});
    ::fixy::check::expect_dies(
        test, [&engine, &av] noexcept { static_cast<void>(engine.output_ptr(0, av)); },
        ::fixy::TextView{"contract violation: matched.is_some()"});
    CRUCIBLE_FATAL_INVARIANT(engine.advance(SchemaHash{700}, ShapeHash{800}, av).status() == ReplayStatus::MATCH);
    require_matched(engine, 0);

    ::fixy::check::pass(kTest, "  test_rewind_in_the_middle_drops_the_matched_op: PASSED\n");
}

// A DIVERGED step matched no op, so a read of its matched position ends the
// process in each build.  The status of the step stays readable.
static void test_diverged_step_has_no_matched_position(::fixy::TestRunnerCtx const& test) {
    auto source = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out_slot = ::fixy::check::run_of(kTest, source.alloc, arena, SlotId{0});
    auto ops_run = arena.array(source.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const ops = ops_run.view();
    for (uint32_t i = 0; i < 2; i++) {
        TraceEntry& op = *ops.get(i).expect("i is below the count of ops");
        op.schema_hash = SchemaHash{900 + i};
        op.shape_hash = ShapeHash{910 + i};
        set_output_slots(op, out_slot);
    }
    const crucible::DagRef<RegionNode> region = build_region(source.alloc, arena, ops_run);
    auto slots_run = arena.array(source.alloc, 1, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(source.alloc, arena, slots_run));
    CrucibleContext ctx{source.alloc};
    auto context_key = crucible::mint_context_key(source);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    ReplayEngine& engine = ctx.engine(source);
    auto av = engine.mint_active_view(kVigilForeground);
    const ReplayStep diverged = engine.advance(SchemaHash{999}, ShapeHash{910}, av);
    CRUCIBLE_FATAL_INVARIANT(diverged.status() == ReplayStatus::DIVERGED);
    ::fixy::check::expect_dies(
        test, [diverged] noexcept { static_cast<void>(diverged.matched_op_index()); },
        ::fixy::TextView{"contract violation: status != ReplayStatus::DIVERGED"});

    ::fixy::check::pass(kTest, "  test_diverged_step_has_no_matched_position: PASSED\n");
}

// The cursor never leaves the region: the advance that completes the region
// takes the cursor back to the first op, so an advance after it, with no
// reset, guards the first op of the next iteration.
static void test_advance_after_the_end_guards_the_first_op() {
    auto source = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out_slot = ::fixy::check::run_of(kTest, source.alloc, arena, SlotId{0});
    auto ops_run = arena.array(source.alloc, 1, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 1> const ops = ops_run.view().window<1>(0).expect("the run holds one op");
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{70};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{71};
    set_output_slots(ops.at(::foundation::core::index<0>()), out_slot);
    const crucible::DagRef<RegionNode> region = build_region(source.alloc, arena, ops_run);
    auto slots_run = arena.array(source.alloc, 1, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(source.alloc, arena, slots_run));
    CrucibleContext ctx{source.alloc};
    auto context_key = crucible::mint_context_key(source);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    ReplayEngine& engine = ctx.engine(source);
    auto av = engine.mint_active_view(kVigilForeground);
    CRUCIBLE_FATAL_INVARIANT(engine.advance(SchemaHash{70}, ShapeHash{71}, av).status() == ReplayStatus::COMPLETE);
    // A hash that differs from the first op diverges at the first op.
    CRUCIBLE_FATAL_INVARIANT(engine.advance(SchemaHash{999}, ShapeHash{999}, av).status() == ReplayStatus::DIVERGED);
    CRUCIBLE_FATAL_INVARIANT(engine.diverged_op_index_tagged().value() == OpIndex{0});
    // The hashes of the first op match it, and complete the region of one op
    // again.
    CRUCIBLE_FATAL_INVARIANT(engine.advance(SchemaHash{70}, ShapeHash{71}, av).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_FATAL_INVARIANT(engine.ops_matched() == 0);

    ::fixy::check::pass(kTest, "  test_advance_after_the_end_guards_the_first_op: PASSED\n");
}

// A slot id of the region past the count of the plan is a defect of the
// build.  The activation refuses it, so no replay reads past the slot table.
static void test_slot_id_past_the_table_aborts_init(::fixy::TestRunnerCtx const& test) {
    auto source = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});
    const auto out_slot = ::fixy::check::run_of(kTest, source.alloc, arena, SlotId{0});
    const auto in_slot = ::fixy::check::run_of(kTest, source.alloc, arena, SlotId{5});
    auto ops_run = arena.array(source.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry, 2> const ops = ops_run.view().window<2>(0).expect("the run holds two ops");
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{80};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{81};
    set_output_slots(ops.at(::foundation::core::index<0>()), out_slot);
    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{82};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{83};
    set_input_slots(ops.at(::foundation::core::index<1>()), in_slot);
    const crucible::DagRef<RegionNode> region = build_region(source.alloc, arena, ops_run);
    auto slots_run = arena.array(source.alloc, 2, TensorSlot{});
    region->plan = crucible::make_plan_link(make_simple_plan(source.alloc, arena, slots_run));
    ::fixy::check::expect_dies(
        test,
        [region] noexcept {
            CrucibleContext ctx{::foundation::effects::testing::test().alloc};
            auto context_key = crucible::mint_context_key(::foundation::effects::testing::test());
            static_cast<void>(ctx.activate(context_key, *region));
        },
        ::fixy::TextView{"op 1 has the input slot id 5"});

    ::fixy::check::pass(kTest, "  test_slot_id_past_the_table_aborts_init: PASSED\n");
}

// The plan here comes from the real sweep-line planner rather than the
// fixed-stride helper above, so the offsets are whatever the planner
// chooses and the assertions never name one.
static void test_integration_with_pool() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{4096});

    // Slot 2 is external: it stands for a parameter whose storage the
    // pool does not own and must not place.
    constexpr uint32_t NSLOTS = 3;
    auto slots_run = arena.array(test.alloc, NSLOTS, TensorSlot{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TensorSlot, NSLOTS> const slots =
        slots_run.view().window<NSLOTS>(0).expect("the run holds NSLOTS slots");
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
    ops.at(::foundation::core::index<0>()).schema_hash = SchemaHash{0xAAAA};
    ops.at(::foundation::core::index<0>()).shape_hash = ShapeHash{0xBBBB};
    set_output_slots(ops.at(::foundation::core::index<0>()), op0_out);
    set_input_slots(ops.at(::foundation::core::index<0>()), op0_in);

    ops.at(::foundation::core::index<1>()).schema_hash = SchemaHash{0xCCCC};
    ops.at(::foundation::core::index<1>()).shape_hash = ShapeHash{0xDDDD};
    set_output_slots(ops.at(::foundation::core::index<1>()), op1_out);
    set_input_slots(ops.at(::foundation::core::index<1>()), op1_in);

    const crucible::DagRef<RegionNode> region = build_region(test.alloc, arena, ops_run);
    // The plan is in the arena of the slots.
    region->plan = crucible::make_plan_link(plan_storage);

    CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    alignas(256) char fake_param[128];
    const crucible::TensorData param_data = crucible::test::host_data(fake_param);
    const auto registered = ctx.with_compiled(context_key, [param_data](crucible::CompiledScope& scope) noexcept {
        scope.register_external(SlotId{2}, param_data).expect("the plan marks slot 2 external");
    });
    CRUCIBLE_TEST_ASSERT(kTest, registered.is_some());
    const PoolAllocator& pool = ctx.pool();
    auto pv = pool.mint_initialized_view(kVigilForeground);

    ReplayEngine& engine = ctx.engine(test);
    auto av = engine.mint_active_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest,
                         engine.advance(SchemaHash{0xAAAA}, ShapeHash{0xBBBB}, av).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, engine.output_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));
    // The external slot resolves to registered storage, not into the pool.
    CRUCIBLE_TEST_ASSERT(kTest, crucible::test::host_ptr(engine.input_ptr(0, av)) == fake_param);

    std::memset(crucible::test::host_ptr(engine.output_ptr(0, av)), 0x11, 512);

    CRUCIBLE_TEST_ASSERT(kTest,
                         engine.advance(SchemaHash{0xCCCC}, ShapeHash{0xDDDD}, av).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_TEST_ASSERT(kTest, engine.output_ptr(0, av) == pool.slot_ptr(SlotId{1}, pv));
    CRUCIBLE_TEST_ASSERT(kTest, engine.input_ptr(0, av) == pool.slot_ptr(SlotId{0}, pv));

    // Reading op 1's input back finds op 0's fill, which is what proves
    // the two entries name the same pool bytes.
    auto* p = crucible::test::host_ptr<uint8_t>(engine.input_ptr(0, av));
    for (uint32_t i = 0; i < 512; i++)
        CRUCIBLE_TEST_ASSERT(kTest, p[i] == 0x11);

    CRUCIBLE_TEST_ASSERT(kTest, engine.ops_matched() == 0);

    engine.reset(av);
    CRUCIBLE_TEST_ASSERT(kTest, engine.ops_matched() == 0);
    CRUCIBLE_TEST_ASSERT(kTest,
                         engine.advance(SchemaHash{0xAAAA}, ShapeHash{0xBBBB}, av).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest,
                         engine.advance(SchemaHash{0xCCCC}, ShapeHash{0xDDDD}, av).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_TEST_ASSERT(kTest, engine.ops_matched() == 0);

    ::fixy::check::pass(kTest, "  test_integration_with_pool: PASSED\n");
}

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::report(::fixy::Sink::Out, "test_replay_engine:\n");
    test_linear_match();
    test_schema_divergence();
    test_shape_divergence();
    test_reset();
    test_input_ptr();
    test_invalid_slot();
    test_current_entry();
    test_matched_index_after_reset();
    test_matched_index_follows_the_cursor();
    test_rewind_in_the_middle_drops_the_matched_op(test);
    test_diverged_step_has_no_matched_position(test);
    test_advance_after_the_end_guards_the_first_op();
    test_slot_id_past_the_table_aborts_init(test);
    test_integration_with_pool();
    ::fixy::check::pass(kTest, "test_replay_engine: all tests passed\n");
    return 0;
}
