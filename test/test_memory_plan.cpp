#include <crucible/MerkleDag.h>
#include <crucible/BackgroundThread.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <cstdint>

namespace {

// One internal slot with the given lifetime, and one internal slot with a
// lifetime that the planner accepts.
void plan_two_slots(std::uint32_t birth_op, std::uint32_t death_op) {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{4096};
    crucible::BackgroundThread bt{::foundation::effects::testing::test().alloc};
    ::fixy::View<crucible::TensorSlot, 2> const slots =
        arena.array(test.alloc, 2, crucible::TensorSlot{}).window<2>(0).expect("the run holds two slots");
    crucible::TensorSlot& first_slot = slots.at(::foundation::core::index<0>());
    first_slot.nbytes = 1024;
    first_slot.birth_op = crucible::OpIndex{0};
    first_slot.death_op = crucible::OpIndex{2};
    first_slot.slot_id = crucible::SlotId{0};
    crucible::TensorSlot& second_slot = slots.at(::foundation::core::index<1>());
    second_slot.nbytes = 1024;
    second_slot.birth_op = crucible::OpIndex{birth_op};
    second_slot.death_op = crucible::OpIndex{death_op};
    second_slot.slot_id = crucible::SlotId{1};
    crucible::MemoryPlan plan_storage{};
    ::fixy::BumpArena sweep_arena{size_t{1} << 16};
    const crucible::MemoryPlan& plan = *bt.compute_memory_plan(test.alloc, plan_storage, sweep_arena, slots)
                                            .expect("the slots of the test fit in 64 bits");
    ::fixy::report(::fixy::Sink::Out, "  the planner returned a plan of {} bytes\n",
                   static_cast<unsigned long long>(plan.pool_bytes));
}

// The refusal of the planner for two internal slots of the given spans, which
// live at the same time, or no refusal.
[[nodiscard]] ::fixy::Option<crucible::BackgroundThread::PlanRefusal> refusal_of_spans(std::uint64_t first_nbytes,
                                                                                       std::uint64_t second_nbytes) {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{4096};
    crucible::BackgroundThread bt{test.alloc};
    ::fixy::View<crucible::TensorSlot, 2> const slots =
        arena.array(test.alloc, 2, crucible::TensorSlot{}).window<2>(0).expect("the run holds two slots");
    crucible::TensorSlot& first_slot = slots.at(::foundation::core::index<0>());
    first_slot.nbytes = first_nbytes;
    first_slot.birth_op = crucible::OpIndex{0};
    first_slot.death_op = crucible::OpIndex{2};
    first_slot.slot_id = crucible::SlotId{0};
    crucible::TensorSlot& second_slot = slots.at(::foundation::core::index<1>());
    second_slot.nbytes = second_nbytes;
    second_slot.birth_op = crucible::OpIndex{1};
    second_slot.death_op = crucible::OpIndex{2};
    second_slot.slot_id = crucible::SlotId{1};
    crucible::MemoryPlan plan_storage{};
    ::fixy::BumpArena sweep_arena{size_t{1} << 16};
    return bt.compute_memory_plan(test.alloc, plan_storage, sweep_arena, slots).err();
}

// The refusal of the planner for an internal slot and an external slot with
// the two ids, in that order, or no refusal.
[[nodiscard]] ::fixy::Option<crucible::BackgroundThread::PlanRefusal>
refusal_of_ids(::foundation::core::FixedArray<crucible::SlotId, 2> const& ids) {
    auto test = ::foundation::effects::testing::test();
    crucible::BackgroundThread bt{test.alloc};
    ::foundation::core::FixedArray<crucible::TensorSlot, 2> slots{};
    crucible::TensorSlot& internal_slot = slots.at(::foundation::core::index<0>());
    internal_slot.nbytes = 1024;
    internal_slot.birth_op = crucible::OpIndex{0};
    internal_slot.death_op = crucible::OpIndex{2};
    internal_slot.slot_id = ids.at(::foundation::core::index<0>());
    crucible::TensorSlot& external_slot = slots.at(::foundation::core::index<1>());
    external_slot.is_external = true;
    external_slot.slot_id = ids.at(::foundation::core::index<1>());
    crucible::MemoryPlan plan_storage{};
    ::fixy::BumpArena sweep_arena{size_t{1} << 16};
    return bt.compute_memory_plan(test.alloc, plan_storage, sweep_arena, slots.view()).err();
}

// True when the planner gave the expected refusal.
[[nodiscard]] bool is_refusal(::fixy::Option<crucible::BackgroundThread::PlanRefusal> refusal,
                              crucible::BackgroundThread::PlanRefusal expected) noexcept {
    return refusal.match([expected](crucible::BackgroundThread::PlanRefusal got) noexcept { return got == expected; },
                         []() noexcept { return false; });
}

// A span that saturated to 2^64 - 1 rounds past 64 bits, and two spans of
// 2^63 that live at the same time end the pool at 2^64.  The planner refuses
// each, so the region has no plan and stays on eager.
void test_refuses_spans_past_64_bits(::fixy::TestRunnerCtx const& test) {
    using Refusal = crucible::BackgroundThread::PlanRefusal;
    CRUCIBLE_TEST_ASSERT(test, is_refusal(refusal_of_spans(~std::uint64_t{0}, 1024), Refusal::SpanTooLarge));
    CRUCIBLE_TEST_ASSERT(test, is_refusal(refusal_of_spans(1024, ~std::uint64_t{0} - 100), Refusal::SpanTooLarge));
    CRUCIBLE_TEST_ASSERT(
        test, is_refusal(refusal_of_spans(std::uint64_t{1} << 63, std::uint64_t{1} << 63), Refusal::PoolEndOverflow));
    CRUCIBLE_TEST_ASSERT(test, refusal_of_spans(1024, 2048).is_none());
    ::fixy::check::pass(test, "  test_refuses_spans_past_64_bits: PASSED\n");
}

// The ops name a slot by its position, so the planner refuses each slot whose
// id is not its position: an internal slot or an external slot, a swapped
// pair, and the default id.  The slots in place give a plan.
void test_refuses_slot_ids_out_of_place(::fixy::TestRunnerCtx const& test) {
    using Refusal = crucible::BackgroundThread::PlanRefusal;
    using Ids = ::foundation::core::FixedArray<crucible::SlotId, 2>;
    CRUCIBLE_TEST_ASSERT(test, refusal_of_ids(Ids{crucible::SlotId{0}, crucible::SlotId{1}}).is_none());
    CRUCIBLE_TEST_ASSERT(
        test, is_refusal(refusal_of_ids(Ids{crucible::SlotId{1}, crucible::SlotId{0}}), Refusal::SlotOutOfPlace));
    CRUCIBLE_TEST_ASSERT(
        test, is_refusal(refusal_of_ids(Ids{crucible::SlotId{0}, crucible::SlotId{7}}), Refusal::SlotOutOfPlace));
    CRUCIBLE_TEST_ASSERT(
        test, is_refusal(refusal_of_ids(Ids{crucible::SlotId{0}, crucible::SlotId{}}), Refusal::SlotOutOfPlace));
    ::fixy::check::pass(test, "  test_refuses_slot_ids_out_of_place: PASSED\n");
}

// The sizes of the sweep come from the largest death_op + 2.  A death_op of
// UINT32_MAX - 1 makes that sum wrap to zero.
void plan_wrapping_death() { plan_two_slots(0, UINT32_MAX - 1); }

// The none() value of OpIndex as a death.  Its death_op + 1 wraps to zero,
// so the slot does not grow the sweep, and its birth indexes past it.
void plan_death_none() { plan_two_slots(5, UINT32_MAX); }

// A birth after the death.  The sweep is sized from the deaths, so the birth
// indexes past it.
void plan_birth_after_death() { plan_two_slots(9, 1); }

// The planner rejects each slot lifetime that its sweep cannot index, and it
// stops the process with a report that names the broken rule.  Each attack
// runs in a child process through expect_dies of the Check family.
void test_refuses_slot_lifetimes_outside_the_sweep(::fixy::TestRunnerCtx const& test) {
    constexpr ::fixy::TextView kMaxDeath{"MAX_DEATH_OP"};
    ::fixy::check::expect_dies(test, plan_wrapping_death, kMaxDeath);
    ::fixy::check::expect_dies(test, plan_death_none, kMaxDeath);
    ::fixy::check::expect_dies(test, plan_birth_after_death, ::fixy::TextView{"birth_op.raw() <= slot.death_op.raw()"});
    ::fixy::check::pass(test, "  test_refuses_slot_lifetimes_outside_the_sweep: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_refuses_slot_lifetimes_outside_the_sweep(test);
    test_refuses_spans_past_64_bits(test);
    test_refuses_slot_ids_out_of_place(test);
    auto source = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{4096};
    crucible::BackgroundThread bt{::foundation::effects::testing::test().alloc};

    // The intervals are chosen so slot 2 can reuse slot 1's space: slot 1 dies at
    // op 3 and slot 2 is born at op 4.  Slot 3 is external and takes no pool
    // space.
    constexpr uint32_t N = 4;
    ::fixy::View<crucible::TensorSlot, N> const slots =
        arena.array(source.alloc, N, crucible::TensorSlot{}).window<N>(0).expect("the run holds N slots");

    using crucible::SlotId;
    using crucible::OpIndex;
    using crucible::ScalarType;
    using crucible::DeviceType;
    using crucible::Layout;
    slots.at(::foundation::core::index<0>()) = {.offset_bytes = 0,
                                                .nbytes = 1024,
                                                .birth_op = OpIndex{0},
                                                .death_op = OpIndex{5},
                                                .dtype = ScalarType::Float,
                                                .device_type = DeviceType::CPU,
                                                .device_idx = 0,
                                                .layout = Layout::Strided,
                                                .is_external = false,
                                                .pad = {},
                                                .slot_id = SlotId{0},
                                                .pad2 = {}};
    slots.at(::foundation::core::index<1>()) = {.offset_bytes = 0,
                                                .nbytes = 2048,
                                                .birth_op = OpIndex{1},
                                                .death_op = OpIndex{3},
                                                .dtype = ScalarType::Float,
                                                .device_type = DeviceType::CPU,
                                                .device_idx = 0,
                                                .layout = Layout::Strided,
                                                .is_external = false,
                                                .pad = {},
                                                .slot_id = SlotId{1},
                                                .pad2 = {}};
    slots.at(::foundation::core::index<2>()) = {.offset_bytes = 0,
                                                .nbytes = 1024,
                                                .birth_op = OpIndex{4},
                                                .death_op = OpIndex{7},
                                                .dtype = ScalarType::Float,
                                                .device_type = DeviceType::CPU,
                                                .device_idx = 0,
                                                .layout = Layout::Strided,
                                                .is_external = false,
                                                .pad = {},
                                                .slot_id = SlotId{2},
                                                .pad2 = {}};
    slots.at(::foundation::core::index<3>()) = {.offset_bytes = 0,
                                                .nbytes = 512,
                                                .birth_op = OpIndex{0},
                                                .death_op = OpIndex{7},
                                                .dtype = ScalarType::Float,
                                                .device_type = DeviceType::CPU,
                                                .device_idx = 0,
                                                .layout = Layout::Strided,
                                                .is_external = true,
                                                .pad = {},
                                                .slot_id = SlotId{3},
                                                .pad2 = {}};

    crucible::MemoryPlan plan_storage{};
    ::fixy::BumpArena sweep_arena{size_t{1} << 16};
    const crucible::MemoryPlan& plan = *bt.compute_memory_plan(source.alloc, plan_storage, sweep_arena, slots)
                                            .expect("the slots of the test fit in 64 bits");
    // The planner writes the plan into the storage of the caller, and the
    // result reaches that storage.
    CRUCIBLE_TEST_ASSERT(test, ::fixy::same_object(::fixy::to(plan), ::fixy::to(plan_storage)));
    CRUCIBLE_TEST_ASSERT(test, plan.num_slots() == N);
    CRUCIBLE_TEST_ASSERT(test, plan.num_external == 1);
    CRUCIBLE_TEST_ASSERT(test, plan.pool_bytes > 0);

    CRUCIBLE_TEST_ASSERT(test, slots.at(::foundation::core::index<2>()).offset_bytes
                                   <= slots.at(::foundation::core::index<1>()).offset_bytes + 2048);

    uint64_t total_internal = 1024 + 2048 + 1024;
    CRUCIBLE_TEST_ASSERT(test, plan.pool_bytes < total_internal);

    // At every op boundary the simultaneously-live slots must hold pairwise
    // disjoint byte intervals.  The loop runs to 7 because that is the largest
    // death_op among the slots.  The helper excludes external slots, so only the
    // internal live set is checked.
    using crucible::live_intervals_disjoint_at;
    for (uint32_t t = 0; t <= 7; ++t) {
        bool disjoint = live_intervals_disjoint_at<4>(slots, OpIndex{t});
        CRUCIBLE_TEST_ASSERT(test, disjoint);
    }

    ::fixy::check::pass(test, "test_memory_plan: all tests passed\n");
    ::fixy::report(::fixy::Sink::Out, "  pool_bytes: {}\n", static_cast<unsigned long>(plan.pool_bytes));
    ::fixy::report(::fixy::Sink::Out, "  slot offsets: [{}, {}, {}, {}]\n",
                   static_cast<unsigned long>(slots.at(::foundation::core::index<0>()).offset_bytes),
                   static_cast<unsigned long>(slots.at(::foundation::core::index<1>()).offset_bytes),
                   static_cast<unsigned long>(slots.at(::foundation::core::index<2>()).offset_bytes),
                   static_cast<unsigned long>(slots.at(::foundation::core::index<3>()).offset_bytes));
    return 0;
}
