// The record slots of the producer thread are a stack of frames.  An adapter
// opens one OpRecordFrame for each op, and an op can run other ops while its
// frame is open.  A frame that opens while another frame is open starts past
// the slots of that frame, so the records of the outer op stay as the adapter
// wrote them.

#include <crucible/TensorMeta.h>
#include <crucible/ValidatedOp.h>
#include <fixy/Core.h>

#include "test_assert.h"

#include <cstdint>

namespace {

// Writes the ranks first_rank, first_rank + 1 and so on into the slots.
void write_ranks(::fixy::View<crucible::TensorMeta> slots, uint8_t first_rank) noexcept {
    uint8_t rank = first_rank;
    for (crucible::TensorMeta& slot : slots) {
        slot = crucible::TensorMeta{};
        slot.ndim = rank;
        ++rank;
    }
}

// True when the slots hold the ranks first_rank, first_rank + 1 and so on.
[[nodiscard]] bool has_ranks(::fixy::View<const crucible::TensorMeta> slots, uint8_t first_rank) noexcept {
    uint8_t rank = first_rank;
    for (const crucible::TensorMeta& slot : slots) {
        if (slot.ndim != rank) return false;
        ++rank;
    }
    return true;
}

// The slots of an open frame.
[[nodiscard]] ::fixy::View<crucible::TensorMeta> slots_of(crucible::OpRecordFrame const& frame) noexcept {
    return frame.slots().expect("the frame is open");
}

// An op that runs inside another op writes the slots past the frame of the
// outer op.  The outer op reads its own records after the inner op.
void test_a_nested_frame_keeps_the_records_of_the_outer_frame() {
    crucible::OpRecordStack stack{};
    const crucible::OpRecordFrame outer{stack, 3};
    write_ranks(slots_of(outer), 1);
    {
        const crucible::OpRecordFrame inner{stack, 2};
        assert(inner.is_open() && "a frame of two slots fits past a frame of three");
        write_ranks(slots_of(inner), 5);
        assert(has_ranks(slots_of(inner), 5) && "the inner frame reads its own records");
        {
            const crucible::OpRecordFrame innermost{stack, 4};
            write_ranks(slots_of(innermost), 7);
            assert(stack.held() == 9 && "the three open frames hold their slots");
        }
        assert(has_ranks(slots_of(inner), 5) && "a nested frame writes no slot of the frame around it");
    }
    assert(has_ranks(slots_of(outer), 1) && "a nested frame writes no slot of the frame around it");
    assert(stack.held() == 3 && "a closed frame gives its slots back");
}

// Each frame opens at the top of the stack, and the close of a frame gives
// the top back.  The next op then uses the same slots again.
void test_a_closed_frame_gives_its_slots_back() {
    crucible::OpRecordStack stack{};
    assert(stack.held() == 0 && "a new stack holds no slot");
    {
        const crucible::OpRecordFrame first{stack, 5};
        write_ranks(slots_of(first), 1);
        assert(stack.held() == 5 && "an open frame holds its slots");
    }
    assert(stack.held() == 0 && "a closed frame gives its slots back");
    const crucible::OpRecordFrame second{stack, 2};
    assert(slots_of(second).size() == 2 && "a frame gives exactly its count of slots");
    write_ranks(slots_of(second), 3);
    assert(has_ranks(slots_of(second), 3) && "the next op writes the same slots again");
}

// A frame that does not fit the free slots holds no slot.  It gives no
// value, and it does not move the top, so the frames that open after it
// still fit.
void test_a_frame_that_does_not_fit_holds_no_slot() {
    crucible::OpRecordStack stack{};
    {
        const crucible::OpRecordFrame too_large{stack, crucible::kMaxOpRecords + 1};
        assert(!too_large.is_open() && "a frame past the slots of the stack does not open");
        assert(too_large.slots().is_none() && "a frame that is not open gives no slot");
        assert(stack.held() == 0 && "a frame that is not open holds no slot");
    }
    const crucible::OpRecordFrame whole{stack, crucible::kMaxOpRecords};
    assert(whole.is_open() && "a frame of each slot fits an empty stack");
    {
        const crucible::OpRecordFrame one_more{stack, 1};
        assert(!one_more.is_open() && "a full stack has no slot for a frame of one");
        const crucible::OpRecordFrame empty{stack, 0};
        assert(empty.is_open() && "a frame of no slot fits a full stack");
        assert(slots_of(empty).size() == 0 && "a frame of no slot gives an empty View");
    }
    assert(stack.held() == crucible::kMaxOpRecords && "the frames that did not fit gave nothing back");
}

}  // namespace

int main() {
    test_a_nested_frame_keeps_the_records_of_the_outer_frame();
    test_a_closed_frame_gives_its_slots_back();
    test_a_frame_that_does_not_fit_holds_no_slot();
    crucible::test::pass("test_op_record_stack: PASS\n");
    return 0;
}
