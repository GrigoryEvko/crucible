// The record slots of the producer thread are a stack of frames.  An adapter
// opens one frame for each op through the frame door of the stack, and an op
// can run other ops while its frame is open.  A frame that opens while
// another frame is open starts past the slots of that frame, so the records
// of the outer op stay as the adapter wrote them.

#include <crucible/TensorMeta.h>
#include <crucible/ValidatedOp.h>
#include <fixy/Core.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::foundation::core::WindowCount;

// Writes the marks first_rank, first_rank + 1 and so on into the output
// index of the slots of a frame.  The output index is a plain byte, so a mark
// can pass the rank bound of a descriptor.
template <class Slots>
void write_ranks(Slots& slots, uint8_t first_rank) noexcept {
    uint8_t rank = first_rank;
    for (crucible::TensorMeta& slot : slots) {
        slot = crucible::TensorMeta{};
        slot.output_nr = rank;
        ++rank;
    }
}

// True when the slots hold the marks first_rank, first_rank + 1 and so on.
[[nodiscard]] bool has_ranks(::fixy::View<const crucible::TensorMeta> slots, uint8_t first_rank) noexcept {
    uint8_t rank = first_rank;
    for (const crucible::TensorMeta& slot : slots) {
        if (slot.output_nr != rank) return false;
        ++rank;
    }
    return true;
}

// An op that runs inside another op writes the slots past the frame of the
// outer op.  The outer op reads its own records after the inner op.  The arm
// of an outer frame reaches the stack by a reference that is not const, and
// a slot holds the data pointer of a tensor, so the store check of a frame
// door refuses such an arm.  The adapters nest their frames through the
// Vigil in foreign frames, and so do the outer frames here.
void test_a_nested_frame_keeps_the_records_of_the_outer_frame() {
    crucible::OpRecordStack stack{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the arms of nested frames reach the record stack, as in the vessel")
    const bool is_kept = stack.with_foreign_frame(
        WindowCount{3},
        [&stack](auto outer) noexcept {
            write_ranks(outer, 1);
            const bool is_inner_kept = stack.with_foreign_frame(
                WindowCount{2},
                [&stack](auto inner) noexcept {
                    write_ranks(inner, 5);
                    const bool is_inner_read = has_ranks(inner.view(), 5);
                    const crucible::OpRecordStack& reader = stack;
                    const uint32_t innermost_held = stack.with_frame(
                        WindowCount{4},
                        [&reader](auto innermost) noexcept {
                            write_ranks(innermost, 7);
                            return reader.held();
                        },
                        [] noexcept { return uint32_t{0}; });
                    CRUCIBLE_TEST_ASSERT(kTest, innermost_held == 9 && "the three open frames hold their slots");
                    return is_inner_read && has_ranks(inner.view(), 5);
                },
                [] noexcept { return false; });
            CRUCIBLE_TEST_ASSERT(kTest, is_inner_kept && "a nested frame writes no slot of the frame around it");
            return has_ranks(outer.view(), 1) && stack.held() == 3;
        },
        [] noexcept { return false; });
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_TEST_ASSERT(kTest, is_kept && "a nested frame writes no slot of the frame around it");
    CRUCIBLE_TEST_ASSERT(kTest, stack.held() == 0 && "a closed frame gives its slots back");
}

// Each frame opens at the top of the stack, and the close of a frame gives
// the top back.  The next op then uses the same slots again.
void test_a_closed_frame_gives_its_slots_back() {
    crucible::OpRecordStack stack{};
    CRUCIBLE_TEST_ASSERT(kTest, stack.held() == 0 && "a new stack holds no slot");
    const crucible::OpRecordStack& reader = stack;
    const uint32_t first_held = stack.with_frame(
        WindowCount{5},
        [&reader](auto first) noexcept {
            write_ranks(first, 1);
            return reader.held();
        },
        [] noexcept { return uint32_t{0}; });
    CRUCIBLE_TEST_ASSERT(kTest, first_held == 5 && "an open frame holds its slots");
    CRUCIBLE_TEST_ASSERT(kTest, stack.held() == 0 && "a closed frame gives its slots back");
    const bool is_written_again = stack.with_frame(
        WindowCount{2},
        [](auto second) noexcept {
            const bool is_exact = second.size() == 2;
            write_ranks(second, 3);
            return is_exact && has_ranks(second.view(), 3);
        },
        [] noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_written_again && "the next op writes exactly its count of the same slots again");
}

// A frame that does not fit the free slots holds no slot.  It runs the
// closed arm, and it does not move the top, so the frames that open after it
// still fit.
void test_a_frame_that_does_not_fit_holds_no_slot() {
    crucible::OpRecordStack stack{};
    const bool is_refused = stack.with_frame(
        WindowCount{crucible::kMaxOpRecords + 1}, [](auto) noexcept { return false; }, [] noexcept { return true; });
    CRUCIBLE_TEST_ASSERT(kTest, is_refused && "a frame past the slots of the stack does not open");
    CRUCIBLE_TEST_ASSERT(kTest, stack.held() == 0 && "a frame that is not open holds no slot");
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the arm of a full frame reaches the record stack, as in the vessel")
    const bool is_whole = stack.with_foreign_frame<crucible::kMaxOpRecords>(
        [&stack](auto whole) noexcept {
            const bool is_one_refused =
                stack.with_frame(WindowCount{1}, [](auto) noexcept { return false; }, [] noexcept { return true; });
            const bool is_empty_open = stack.with_frame(
                WindowCount{0}, [](auto empty) noexcept { return empty.size() == 0; }, [] noexcept { return false; });
            return whole.size() == crucible::kMaxOpRecords && is_one_refused && is_empty_open
                && stack.held() == crucible::kMaxOpRecords;
        },
        [] noexcept { return false; });
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_TEST_ASSERT(kTest, is_whole && "a full stack refuses a frame of one and opens a frame of no slot");
    CRUCIBLE_TEST_ASSERT(kTest, stack.held() == 0 && "the frames that did not fit gave nothing back");
}

}  // namespace

int main() {
    test_a_nested_frame_keeps_the_records_of_the_outer_frame();
    test_a_closed_frame_gives_its_slots_back();
    test_a_frame_that_does_not_fit_holds_no_slot();
    ::fixy::check::pass(kTest, "test_op_record_stack: PASS\n");
    return 0;
}
