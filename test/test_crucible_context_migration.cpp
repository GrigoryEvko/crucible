// A switch of region copies the slots of the shared prefix into the new pool,
// and a bitset of MIGRATION_MAX_SLOTS bits records each slot that it copied.
// A new slot smaller than its old slot takes only the bytes that it holds.
// A plan with more slots than the bitset is refused before a slot is copied,
// under each contract semantic.  The build compiles this file twice: one time
// under the contract semantic of the preset, and one time under the ignore
// semantic, where no contract assertion checks and a CRUCIBLE_PRE gives its
// condition to the optimizer as an assumption.
//
// Each refusal ends the process, so each attack runs in a child process
// through expect_dies of the Check family, which requires SIGABRT and a report
// that names the bound.

#include <crucible/CrucibleContext.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include <foundation/effects/Effect.h>

#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The foreground context of the Vigil that the test stands for.
constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

}  // namespace

namespace {

using crucible::CrucibleContext;
using crucible::DeviceType;
using crucible::Layout;
using crucible::MemoryPlan;
using crucible::OpIndex;
using crucible::RegionNode;
using crucible::ScalarType;
using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::SlotId;
using crucible::TensorMeta;
using crucible::TensorSlot;
using crucible::TraceEntry;
using crucible::TraceNodeKind;

// The bound of the bitset in CrucibleContext.
constexpr std::uint32_t kBitsetSlots = 1024;
// A plan two times as wide as the bitset.
constexpr std::uint32_t kWideSlots = 2 * kBitsetSlots;
constexpr std::uint64_t kSlotBytes = 256;

// Every slot is internal and kSlotBytes wide, so slot i sits at offset
// i * kSlotBytes and no two slots overlap.  The plan is in the arena.
crucible::DagRef<MemoryPlan> make_plan(::foundation::effects::Alloc alloc, ::fixy::BumpArena<crucible::DagArena>& arena,
                                       ::fixy::ArenaRun<TensorSlot, crucible::DagArena> slots) {
    const auto count = static_cast<std::uint32_t>(slots.size());
    std::uint32_t index = 0;
    for (TensorSlot& slot : slots.view()) {
        slot = {.offset_bytes = index * kSlotBytes,
                .nbytes = kSlotBytes,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{2},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{index},
                .pad2 = {}};
        ++index;
    }
    const crucible::DagRef<MemoryPlan> plan = arena.make<MemoryPlan>(alloc);
    plan->slots = slots;
    plan->num_external = 0;
    plan->pool_bytes = count * kSlotBytes;
    plan->device_type = DeviceType::CPU;
    plan->device_idx = 0;
    return plan;
}

// The metadata of the outputs and the slots of the plans are runs of this
// arena, which lives as long as the process.  A block holds the 2048 slots of
// the wide plan.  Each function takes its runs in its own body, so no object
// of static storage holds a run.
auto g_test = ::foundation::effects::testing::test();
using Bytes = ::fixy::Quantity<::fixy::unit::Byte, std::size_t>;
::fixy::BumpArena<crucible::DagArena> g_runs = ::fixy::mint_bump_arena<crucible::DagArena>(Bytes{std::size_t{1} << 17});

// Two operations with the same hashes in each region, so the first one is
// the shared prefix of a switch at position 1.  The metadata of their outputs
// is zero, and the switch reads none of it.
void make_ops(::fixy::View<TraceEntry> ops, ::fixy::ArenaRun<SlotId const, crucible::DagArena> outputs) {
    ::fixy::ArenaRun<TensorMeta, crucible::DagArena> const output_metas_run =
        g_runs.array(g_test.alloc, 2, TensorMeta{});
    for (std::uint32_t index = 0; index < 2; index++) {
        TraceEntry& op = *ops.get(index).expect("a test region holds two ops");
        op.schema_hash = SchemaHash{100 + index};
        op.shape_hash = ShapeHash{200 + index};
        op.set_outputs(output_metas_run.window(::fixy::RunStart{::fixy::ElementCount{index}}, ::fixy::ElementCount{1})
                           .expect("a test region has one output for each op"),
                       outputs.window(::fixy::RunStart{::fixy::ElementCount{index}}, ::fixy::ElementCount{1})
                           .expect("a test region has one output for each op"));
    }
}

// A region of the ops and the plan in the arena.  The other fields of the
// region keep their defaults, so the hashes stay zero.
crucible::DagRef<RegionNode> build_region(::foundation::effects::Alloc alloc,
                                          ::fixy::BumpArena<crucible::DagArena>& arena,
                                          ::fixy::ArenaRun<TraceEntry const, crucible::DagArena> ops,
                                          crucible::PlanLink plan) {
    const crucible::DagRef<RegionNode> region = crucible::mint_dag_node<RegionNode>(alloc, arena);
    region->kind = TraceNodeKind::REGION;
    region->ops = ops;
    region->plan = plan;
    return region;
}

// Activates a region of two slots, then switches at position 1 to a region
// whose plan has new_count slots.  The first operation of the new region
// writes slot new_slot.  Returns the result of the switch.
bool switch_to_plan(std::uint32_t new_count, std::uint32_t new_slot) {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(Bytes{4096});
    // The slots of the plans are runs of g_runs: the wide plan has 2048 slots
    // of 48 bytes, too many for the stack of a test.
    ::fixy::ArenaRun<TensorSlot, crucible::DagArena> old_slots_run = g_runs.array(g_test.alloc, 2, TensorSlot{});
    ::fixy::ArenaRun<TensorSlot, crucible::DagArena> new_slots_run =
        g_runs.array(g_test.alloc, kWideSlots, TensorSlot{});
    const auto old_outputs = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0}, SlotId{1});
    auto old_ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const old_ops = old_ops_run.view();
    make_ops(old_ops, old_outputs);
    const crucible::DagRef<MemoryPlan> old_plan = make_plan(test.alloc, arena, old_slots_run);
    const crucible::DagRef<RegionNode> old_region =
        build_region(test.alloc, arena, old_ops_run, crucible::make_plan_link(old_plan));

    const auto new_outputs = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{new_slot}, SlotId{0});
    auto new_ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const new_ops = new_ops_run.view();
    make_ops(new_ops, new_outputs);
    const crucible::DagRef<MemoryPlan> new_plan =
        make_plan(test.alloc, arena,
                  new_slots_run.window(::fixy::RunStart{::fixy::ElementCount{0}}, ::fixy::ElementCount{new_count})
                      .expect("a test plan holds at most kWideSlots slots"));
    const crucible::DagRef<RegionNode> new_region =
        build_region(test.alloc, arena, new_ops_run, crucible::make_plan_link(new_plan));

    CrucibleContext context{::foundation::effects::testing::test().alloc};
    auto context_key = crucible::mint_context_key(::foundation::effects::testing::test());
    CRUCIBLE_TEST_ASSERT(kTest, context.activate(context_key, *old_region));
    const bool was_switched = context.switch_region(context_key, *new_region, 1);
    return was_switched && ::fixy::same_target(context.active_region(), ::fixy::some(new_region));
}

// The text of each refusal names the bound.
constexpr ::fixy::TextView kBoundName{"MIGRATION_MAX_SLOTS"};

// A plan at the bound of the bitset migrates.  This shows that the attacks
// below fail for the width of the plan and for no other reason.
void test_plan_at_the_bound_migrates() {
    CRUCIBLE_TEST_ASSERT(kTest, switch_to_plan(kBitsetSlots, kBitsetSlots - 1));
    ::fixy::check::pass(kTest, "  test_plan_at_the_bound_migrates: PASSED\n");
}

// The shared prefix writes a slot that the bitset can record, and the plan is
// still too wide.  The switch is refused before it copies the slot.
void test_wide_plan_is_refused(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(test, [] noexcept { (void)switch_to_plan(kWideSlots, 5); }, kBoundName);
    ::fixy::check::pass(kTest, "  test_wide_plan_is_refused: PASSED\n");
}

// The shared prefix writes a slot past the end of the bitset.  Without the
// refusal, the migration sets a bit outside the bitset on the stack.
void test_slot_past_the_bitset_is_refused(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(test, [] noexcept { (void)switch_to_plan(kWideSlots, kBitsetSlots + 476); }, kBoundName);
    ::fixy::check::pass(kTest, "  test_slot_past_the_bitset_is_refused: PASSED\n");
}

// The old slot of the shared prefix holds two times the bytes of its new slot,
// and the new slot ends the new pool.  The switch copies the bytes that the
// new slot holds.  A copy of the whole old slot writes past the end of the
// new pool, which AddressSanitizer reports in a Debug build.
void test_smaller_new_slot_takes_its_own_size() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(Bytes{4096});
    const auto outputs = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0}, SlotId{0});

    auto wide_slot_run = arena.array(test.alloc, 1, TensorSlot{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TensorSlot, 1> const wide_slot = wide_slot_run.view().window<1>(0).expect("the run holds one slot");
    const crucible::DagRef<MemoryPlan> old_plan = make_plan(test.alloc, arena, wide_slot_run);
    wide_slot.at(::fixy::index<0>()).nbytes = 2 * kSlotBytes;
    old_plan->pool_bytes = 2 * kSlotBytes;
    auto old_ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const old_ops = old_ops_run.view();
    make_ops(old_ops, outputs);
    const crucible::DagRef<RegionNode> old_region =
        build_region(test.alloc, arena, old_ops_run, crucible::make_plan_link(old_plan));

    auto narrow_slot_run = arena.array(test.alloc, 1, TensorSlot{});
    const crucible::DagRef<MemoryPlan> new_plan = make_plan(test.alloc, arena, narrow_slot_run);
    auto new_ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const new_ops = new_ops_run.view();
    make_ops(new_ops, outputs);
    const crucible::DagRef<RegionNode> new_region =
        build_region(test.alloc, arena, new_ops_run, crucible::make_plan_link(new_plan));

    CrucibleContext context{::foundation::effects::testing::test().alloc};
    auto context_key = crucible::mint_context_key(::foundation::effects::testing::test());
    CRUCIBLE_TEST_ASSERT(kTest, context.activate(context_key, *old_region));
    // The output of the first op holds each byte of the old slot.
    const auto fill_outputs = [](auto& views) noexcept {
        std::size_t count = 0;
        std::move(views).drain([&count](auto view) noexcept {
            for (std::uint8_t& byte : view) {
                byte = 0x5A;
                ++count;
            }
        });
        return count;
    };
    const auto is_filled = [&fill_outputs](crucible::CompiledScope& scope) noexcept {
        return scope.advance(SchemaHash{100}, ShapeHash{200}).status() == crucible::ReplayStatus::MATCH
            && scope.with_output_views<std::uint8_t>(fill_outputs).is_ok_and([](std::size_t const& count) noexcept {
                   return count == 2 * kSlotBytes;
               });
    };
    CRUCIBLE_TEST_ASSERT(kTest, context.with_compiled(context_key, is_filled) == ::fixy::some(true));
    CRUCIBLE_TEST_ASSERT(kTest, context.switch_region(context_key, *new_region, 1));
    CRUCIBLE_TEST_ASSERT(kTest, context.pool().bytes().size() == kSlotBytes);
    for (const std::uint8_t byte : context.pool().bytes()) {
        CRUCIBLE_TEST_ASSERT(kTest, byte == 0x5A);
    }
    ::fixy::check::pass(kTest, "  test_smaller_new_slot_takes_its_own_size: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::report(::fixy::Sink::Out, "test_crucible_context_migration:\n");
    test_plan_at_the_bound_migrates();
    test_wide_plan_is_refused(test);
    test_slot_past_the_bitset_is_refused(test);
    test_smaller_new_slot_takes_its_own_size();
    ::fixy::check::pass(kTest, "test_crucible_context_migration: all tests passed\n");
    return 0;
}
