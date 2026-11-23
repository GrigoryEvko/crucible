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

#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

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
// i * kSlotBytes and no two slots overlap.
MemoryPlan make_plan(::fixy::View<TensorSlot> slots) {
    const auto count = static_cast<std::uint32_t>(slots.size());
    std::uint32_t index = 0;
    for (TensorSlot& slot : slots) {
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
    MemoryPlan plan{};
    plan.slots = slots;
    plan.num_external = 0;
    plan.pool_bytes = count * kSlotBytes;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;
    return plan;
}

// The metadata of the outputs and the slots of the plans are runs of this
// arena, which lives as long as the process.  The first block holds the 2048
// slots of the wide plan.
auto g_test = ::foundation::effects::testing::test();
using Bytes = ::fixy::Quantity<::fixy::unit::Byte, std::size_t>;
::fixy::BumpArena<crucible::DagArena> g_runs = ::fixy::mint_bump_arena<crucible::DagArena>(Bytes{std::size_t{1} << 17});

// The zero metadata of the outputs of the test ops.  The switch reads no
// metadata, so the ops of each region share it.
::fixy::ArenaRun<TensorMeta, crucible::DagArena> const output_metas_run = g_runs.array(g_test.alloc, 2, TensorMeta{});
// A plain View of an arena run: the View carries no brand of its arena.
::fixy::View<TensorMeta> const output_metas = output_metas_run.view();

// Two operations with the same hashes in each region, so the first one is
// the shared prefix of a switch at position 1.
void make_ops(::fixy::View<TraceEntry> ops, ::fixy::View<SlotId> outputs) {
    for (std::uint32_t index = 0; index < 2; index++) {
        TraceEntry& op = *ops.get(index).expect("a test region holds two ops");
        op.schema_hash = SchemaHash{100 + index};
        op.shape_hash = ShapeHash{200 + index};
        op.set_outputs(output_metas.window(index, 1).expect("a test region has one output for each op"),
                       outputs.window(index, 1).expect("a test region has one output for each op"));
    }
}

void init_region(RegionNode* region, ::fixy::View<TraceEntry> ops, crucible::PlanLink plan) {
    ::new(region) RegionNode{};
    region->kind = TraceNodeKind::REGION;
    region->ops = ops;
    region->plan = plan;
}

// The slots of the plans are runs of g_runs: the wide plan has 2048 slots of
// 48 bytes, too many for the stack of a test.
::fixy::ArenaRun<TensorSlot, crucible::DagArena> const old_slots_run = g_runs.array(g_test.alloc, 2, TensorSlot{});
::fixy::ArenaRun<TensorSlot, crucible::DagArena> const new_slots_run =
    g_runs.array(g_test.alloc, kWideSlots, TensorSlot{});
// A plain View of an arena run: the View carries no brand of its arena.
::fixy::View<TensorSlot> const old_slots = old_slots_run.view();
// A plain View of an arena run: the View carries no brand of its arena.
::fixy::View<TensorSlot> const new_slots = new_slots_run.view();

// Activates a region of two slots, then switches at position 1 to a region
// whose plan has new_count slots.  The first operation of the new region
// writes slot new_slot.  Returns the result of the switch.
bool switch_to_plan(std::uint32_t new_count, std::uint32_t new_slot) {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(Bytes{4096});
    ::fixy::View<SlotId> const old_outputs = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0}, SlotId{1});
    auto const old_ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const old_ops = old_ops_run.view();
    make_ops(old_ops, old_outputs);
    MemoryPlan old_plan = make_plan(old_slots);
    RegionNode old_region{};
    init_region(&old_region, old_ops, crucible::make_plan_link(old_plan));

    ::fixy::View<SlotId> const new_outputs =
        ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{new_slot}, SlotId{0});
    auto const new_ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const new_ops = new_ops_run.view();
    make_ops(new_ops, new_outputs);
    MemoryPlan new_plan =
        make_plan(new_slots.window(0, new_count).expect("a test plan holds at most kWideSlots slots"));
    RegionNode new_region{};
    init_region(&new_region, new_ops, crucible::make_plan_link(new_plan));

    CrucibleContext context{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, context.activate(old_region));
    const bool was_switched = context.switch_region(new_region, 1);
    return was_switched && ::fixy::same_target(context.active_region(), ::fixy::some(::fixy::to(new_region)));
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
    ::fixy::View<SlotId> const outputs = ::fixy::check::run_of(kTest, test.alloc, arena, SlotId{0}, SlotId{0});

    auto const wide_slot_run = arena.array(test.alloc, 1, TensorSlot{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TensorSlot, 1> const wide_slot = wide_slot_run.view().window<1>(0).expect("the run holds one slot");
    MemoryPlan old_plan = make_plan(wide_slot);
    wide_slot.at(::fixy::index<0>()).nbytes = 2 * kSlotBytes;
    old_plan.pool_bytes = 2 * kSlotBytes;
    auto const old_ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const old_ops = old_ops_run.view();
    make_ops(old_ops, outputs);
    RegionNode old_region{};
    init_region(&old_region, old_ops, crucible::make_plan_link(old_plan));

    auto const narrow_slot_run = arena.array(test.alloc, 1, TensorSlot{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TensorSlot> const narrow_slot = narrow_slot_run.view();
    MemoryPlan new_plan = make_plan(narrow_slot);
    auto const new_ops_run = arena.array(test.alloc, 2, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TraceEntry> const new_ops = new_ops_run.view();
    make_ops(new_ops, outputs);
    RegionNode new_region{};
    init_region(&new_region, new_ops, crucible::make_plan_link(new_plan));

    CrucibleContext context{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, context.activate(old_region));
    ::fixy::fill(context.pool().bytes(), std::uint8_t{0x5A});
    CRUCIBLE_TEST_ASSERT(kTest, context.switch_region(new_region, 1));
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
