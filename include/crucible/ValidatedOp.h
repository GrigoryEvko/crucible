#pragma once

// The op that a recording entry point takes: a trace entry that passed the
// trust ladder, and a View of its tensor metadata records.
//
// The entry gives the input count and the output count of the op.  The
// metadata log takes one record for each tensor, the inputs first, and the
// build reads that many records for the op.  A count of records that
// disagrees with the entry makes the build read the records of a different
// op.  mint_validated_op is the one door to a ValidatedOp, and it gives no op
// when the two counts disagree.  Vigil::record_op and Vigil::dispatch_op take
// only a ValidatedOp, so no caller gives them a count that the door did not
// check.

#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>

#include <fixy/Core.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>

#include <cstdint>

namespace crucible {

// The most inputs and the most outputs of one op.  The trust ladder of the
// vessel refuses an entry with more of either (entry_is_well_formed), so the
// records of each op that the ladder admits fit kMaxOpRecords slots.
inline constexpr uint16_t kMaxOpInputs = 64;
inline constexpr uint16_t kMaxOpOutputs = 64;
inline constexpr uint32_t kMaxOpRecords = uint32_t{kMaxOpInputs} + uint32_t{kMaxOpOutputs};

// The slots of an OpRecordStack.
using OpRecordSlots = ::fixy::FixedArray<TensorMeta, kMaxOpRecords>;

class OpRecordFrame;

// The slots into which an adapter writes the records of the ops that run on
// the producer thread.  An op can run other ops before its records are
// complete: the boxed fallback of the vessel reads the inputs of an op, runs
// the op, and then reads its outputs, and the op can dispatch other ops.  So
// the slots are a stack.  An adapter opens one OpRecordFrame for each op, and
// the frame holds its slots at the top of the stack until it closes.  A frame
// that opens while another frame is open starts past the slots of that frame.
// So an op never writes a record of an op that it runs inside.
//
// Each slot holds a TensorMeta from the construction of the stack on.  The
// thread that holds the producer claim of the Vigil is the one thread that
// reads or writes the stack.
class OpRecordStack {
public:
    constexpr OpRecordStack() noexcept = default;

    OpRecordStack(const OpRecordStack&) = delete("each open frame holds a reference to its stack");
    OpRecordStack& operator=(const OpRecordStack&) = delete("each open frame holds a reference to its stack");
    OpRecordStack(OpRecordStack&&) = delete("each open frame holds a reference to its stack");
    OpRecordStack& operator=(OpRecordStack&&) = delete("each open frame holds a reference to its stack");

    // The count of the slots that the open frames hold.
    [[nodiscard]] constexpr uint32_t held() const noexcept { return top_; }

private:
    friend class OpRecordFrame;

    OpRecordSlots slots_{};
    // At most kMaxOpRecords.  A frame opens only when its slots fit.
    uint32_t top_ = 0;
    [[maybe_unused]] ::fixy::FixedArray<uint8_t, 4> pad_{};
};

// The slots of the records of one op, at the top of an OpRecordStack.  The
// constructor takes count slots when they fit in the stack, and the
// destructor gives them back.  A frame that does not fit holds no slot, and
// slots() gives no value.  A frame cannot copy or move, so the frames of one
// stack close in the reverse order of their opens.  A frame lives in the call
// of its op, so an unwind of that call closes the frame too.  O(1) for each
// open and each close.
class [[nodiscard]] OpRecordFrame {
public:
    OpRecordFrame(OpRecordStack& stack, uint32_t count) noexcept
        : stack_{::fixy::to(stack)}, base_{stack.top_}, count_{count}, slots_{take_slots_(stack, count)} {}

    ~OpRecordFrame() {
        if (slots_.is_none()) return;
        // Each frame that opened after this one closed before it, so the top
        // is the end of the slots of this frame.
        CRUCIBLE_FATAL_INVARIANT(stack_->top_ == base_ + count_);
        stack_->top_ = base_;
    }

    OpRecordFrame(const OpRecordFrame&) = delete("a frame closes at the end of the scope of its op");
    OpRecordFrame& operator=(const OpRecordFrame&) = delete("a frame closes at the end of the scope of its op");
    OpRecordFrame(OpRecordFrame&&) = delete("a frame closes at the end of the scope of its op");
    OpRecordFrame& operator=(OpRecordFrame&&) = delete("a frame closes at the end of the scope of its op");

    // True when the frame holds its count slots.
    [[nodiscard]] constexpr bool is_open() const noexcept { return slots_.is_some(); }

    // The count slots of the frame, or no value when the frame holds no
    // slot.  A slot holds the record of an earlier op until the caller
    // writes it, so a reader reads only the slots that the caller wrote.
    [[nodiscard]] constexpr ::fixy::Option<::fixy::View<TensorMeta>> slots() const noexcept { return slots_; }

private:
    // The count slots at the top of the stack, which the top then passes, or
    // no value and no change of the top when they do not fit.  The window is
    // the one fit test of the open.
    [[nodiscard]] static ::fixy::Option<::fixy::View<TensorMeta>> take_slots_(OpRecordStack& stack,
                                                                              uint32_t count) noexcept {
        const uint32_t top = stack.top_;
        // Only a frame moves the top, and only to the end of slots that fit.
        [[assume(top <= kMaxOpRecords)]];
        ::fixy::Option<::fixy::View<TensorMeta>> slots = stack.slots_.view().window(top, count);
        if (slots.is_some()) stack.top_ = top + count;
        return slots;
    }

    ::fixy::Ref<OpRecordStack> stack_;
    uint32_t base_ = 0;
    uint32_t count_ = 0;
    ::fixy::Option<::fixy::View<TensorMeta>> slots_{::fixy::none};
};

namespace detail {
class ValidatedOpDoor;
}  // namespace detail

// An entry that passed the trust ladder, with a View of its metadata
// records.  The count of the records is the size of the View, and it is the
// input count plus the output count of the entry.  The entry and the records
// live in the caller, so an op lives no longer than the call that made it.
class ValidatedOp {
public:
    [[nodiscard]] constexpr const TraceRing::Entry& entry() const noexcept { return *entry_.value(); }

    // The records of the op: the inputs, then the outputs.
    [[nodiscard]] constexpr ::fixy::View<const TensorMeta> metas() const noexcept { return metas_; }

private:
    constexpr ValidatedOp(TraceRing::ValidatedEntryPtr entry, ::fixy::View<const TensorMeta> metas) noexcept
        : entry_{entry}, metas_{metas} {}

    friend class detail::ValidatedOpDoor;

    TraceRing::ValidatedEntryPtr entry_;
    ::fixy::View<const TensorMeta> metas_{};
};

// The one door to a ValidatedOp.  It gives no op when the count of the
// records is not the input count plus the output count of the entry.
[[nodiscard]] constexpr ::fixy::Option<ValidatedOp> mint_validated_op(TraceRing::ValidatedEntryPtr entry,
                                                                      ::fixy::View<const TensorMeta> metas) noexcept;

namespace detail {

// The key of the private constructor of ValidatedOp.  Its one member is
// private, and mint_validated_op is its one friend.  The return type of the
// mint names an Option of a ValidatedOp, and that Option needs a complete
// class.  So the class itself cannot name the mint as its friend.
class ValidatedOpDoor {
    [[nodiscard]] static constexpr ValidatedOp open_(TraceRing::ValidatedEntryPtr entry,
                                                     ::fixy::View<const TensorMeta> metas) noexcept {
        return ValidatedOp{entry, metas};
    }

    friend constexpr ::fixy::Option<ValidatedOp>
    crucible::mint_validated_op(TraceRing::ValidatedEntryPtr entry, ::fixy::View<const TensorMeta> metas) noexcept;
};

}  // namespace detail

[[nodiscard]] constexpr ::fixy::Option<ValidatedOp> mint_validated_op(TraceRing::ValidatedEntryPtr entry,
                                                                      ::fixy::View<const TensorMeta> metas) noexcept {
    CRUCIBLE_PRE(entry.value() != nullptr);
    const TraceRing::Entry& fields = *entry.value();
    const uint64_t expected = uint64_t{fields.num_inputs} + uint64_t{fields.num_outputs};
    if (metas.size() != expected) return ::fixy::none;
    return ::fixy::Option<ValidatedOp>::some(detail::ValidatedOpDoor::open_(entry, metas));
}

}  // namespace crucible
