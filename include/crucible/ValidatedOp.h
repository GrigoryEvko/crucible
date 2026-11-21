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
#include <foundation/core/WindowStack.h>

#include <cstdint>

namespace crucible {

// The most inputs and the most outputs of one op.  The trust ladder of the
// vessel refuses an entry with more of either (entry_is_well_formed), so the
// records of each op that the ladder admits fit kMaxOpRecords slots.
inline constexpr uint16_t kMaxOpInputs = 64;
inline constexpr uint16_t kMaxOpOutputs = 64;
inline constexpr uint32_t kMaxOpRecords = uint32_t{kMaxOpInputs} + uint32_t{kMaxOpOutputs};

// The slots of an OpRecordStack.
using OpRecordSlots = ::foundation::core::WindowStack<TensorMeta, kMaxOpRecords>;

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
//
// The stack also counts the ops whose record is open (OpNest).  An adapter
// opens one OpNest for each op that it records, also for an op that keeps
// its records on its own stack.  The count is the depth of the next op that
// opens.
class OpRecordStack {
public:
    constexpr OpRecordStack() noexcept = default;

    OpRecordStack(const OpRecordStack&) = delete("each open frame holds a reference to its stack");
    OpRecordStack& operator=(const OpRecordStack&) = delete("each open frame holds a reference to its stack");
    OpRecordStack(OpRecordStack&&) = delete("each open frame holds a reference to its stack");
    OpRecordStack& operator=(OpRecordStack&&) = delete("each open frame holds a reference to its stack");

    // The count of the slots that the open frames hold.
    [[nodiscard]] constexpr uint32_t held() const noexcept { return static_cast<uint32_t>(slots_.held()); }

    // The depth of an op that starts now: the count of the open nests.
    [[nodiscard]] constexpr OpDepth depth() const noexcept { return depth_; }

    // An adapter calls it each time it gives an op of the depth to the Vigil,
    // on each path: a recorded op, a replayed op and a closing entry.  A nest
    // reads the two values to tell if its own op reached the Vigil after the
    // ops inside it (OpNest::is_unrecorded).  O(1).
    constexpr void note_given(OpDepth depth) noexcept {
        ++given_;
        last_given_depth_ = depth;
    }

    // The count of the ops that note_given saw, and the depth of the last.
    [[nodiscard]] constexpr uint64_t given() const noexcept { return given_; }
    [[nodiscard]] constexpr OpDepth last_given_depth() const noexcept { return last_given_depth_; }

private:
    friend class OpRecordFrame;
    friend class OpNest;

    // The slots and their top.  A frame takes its slots through a loan of the
    // window stack, which opens only when its slots fit.
    OpRecordSlots slots_{};
    OpDepth depth_{};
    // The depth of the last op that note_given saw.
    OpDepth last_given_depth_{};
    // The count of the ops that note_given saw.  It wraps after 2^64 ops,
    // which a stream of one op in each nanosecond reaches after 584 years.
    uint64_t given_ = 0;
};

// The nest of the record of one op on an OpRecordStack.  The constructor
// takes the depth of the op from the stack, and each op that opens a nest
// before the destructor runs gets a depth one more.  A nest cannot copy or
// move, so the nests of one stack close in the reverse order of their opens.
// A nest lives in the call of its op, so an unwind of that call closes the
// nest too.  O(1) for each open and each close.
class [[nodiscard]] OpNest {
public:
    explicit OpNest(OpRecordStack& stack) noexcept
        : stack_{::fixy::erase_brand(::fixy::to(stack))}, depth_{stack.depth_}, given_at_open_{stack.given_} {
        stack.depth_ = depth_.nested();
    }

    ~OpNest() {
        // Each nest that opened after this one closed before it.
        CRUCIBLE_FATAL_INVARIANT(stack_->depth_ == depth_.nested());
        stack_->depth_ = depth_;
    }

    OpNest(const OpNest&) = delete("a nest closes at the end of the scope of its op");
    OpNest& operator=(const OpNest&) = delete("a nest closes at the end of the scope of its op");
    OpNest(OpNest&&) = delete("a nest closes at the end of the scope of its op");
    OpNest& operator=(OpNest&&) = delete("a nest closes at the end of the scope of its op");

    // The depth of the op of this nest.
    [[nodiscard]] constexpr OpDepth depth() const noexcept { return depth_; }

    // True when an op inside this nest reached the Vigil and the op of this
    // nest did not.  The Vigil takes the ops in post-order, so the last op
    // given since the open is the op of this nest when it reached the Vigil,
    // and an op of a larger depth when it did not: the op raised, or the
    // adapter refused its record.  The adapter then gives the closing entry
    // (kUnrecordedNestSchema of TraceRing.h) at the depth of this nest, so
    // the ops inside it do not pass to the next op.  An op given before the
    // open does not count, also when it is deeper.  O(1).
    [[nodiscard]] bool is_unrecorded() const noexcept {
        return stack_->given_ != given_at_open_ && depth_.raw() < stack_->last_given_depth_.raw();
    }

private:
    ::fixy::Ref<OpRecordStack> stack_;
    OpDepth depth_{};
    uint64_t given_at_open_ = 0;
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
    // The loan of the window stack checks at its end that each frame that
    // opened after this one closed before it.
    OpRecordFrame(OpRecordStack& stack, uint32_t count) noexcept
        : loan_{stack.slots_, ::foundation::core::WindowCount{count}} {}

    OpRecordFrame(const OpRecordFrame&) = delete("a frame closes at the end of the scope of its op");
    OpRecordFrame& operator=(const OpRecordFrame&) = delete("a frame closes at the end of the scope of its op");
    OpRecordFrame(OpRecordFrame&&) = delete("a frame closes at the end of the scope of its op");
    OpRecordFrame& operator=(OpRecordFrame&&) = delete("a frame closes at the end of the scope of its op");

    // True when the frame holds its count slots.
    [[nodiscard]] constexpr bool is_open() const noexcept { return loan_.is_open(); }

    // The count slots of the frame, or no value when the frame holds no
    // slot.  A slot holds the record of an earlier op until the caller
    // writes it, so a reader reads only the slots that the caller wrote.
    [[nodiscard]] constexpr ::fixy::Option<::fixy::View<TensorMeta>> slots() const noexcept { return loan_.window(); }

private:
    ::foundation::core::WindowLoan<TensorMeta, kMaxOpRecords> loan_;
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
