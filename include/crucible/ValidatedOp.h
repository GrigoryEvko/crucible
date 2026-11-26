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
//
// No code writes the entry or the records of an op after its mint.  The op
// holds a copy of the entry.  The records come from a source that has no
// writer for the life of the op: the one writer of the run of a frame, which
// the mint takes by move, or a Sealed list, which no code changes.

#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>

#include <fixy/Core.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Sealed.h>
#include <foundation/core/WindowStack.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible {

// The most inputs and the most outputs of one op.  The trust ladder refuses
// an entry with more of either (entry_is_well_formed), so the records of each
// op that the ladder admits fit kMaxOpRecords slots.
inline constexpr uint16_t kMaxOpInputs = 64;
inline constexpr uint16_t kMaxOpOutputs = 64;
inline constexpr uint32_t kMaxOpRecords = uint32_t{kMaxOpInputs} + uint32_t{kMaxOpOutputs};

// The one validator of an entry from outside the process.  Each door that
// makes an entry from outside values calls it: the C entries and the boxed
// fallback of the vessel (validate_ffi_op), and the trace loader.  So an entry
// that one door admits, each other door admits too.  Each bound names a
// downstream array that the recording pipeline indexes with the field:
//
//   - schema_hash != 0, because 0 is the invalid sentinel and the
//     content hash and the region cache both key on this field
//   - num_inputs at most kMaxOpInputs and num_outputs at most
//     kMaxOpOutputs, so the records of the op fit the slots of an
//     OpRecordStack
//   - num_scalar_args at most 5, the width of Entry::scalar_values.
//     BackgroundThread asserts the same bound when it drains the ring, so
//     a larger count ends the process there.  The validator refuses the
//     entry first.
//   - each data slot below num_scalar_args, and no bit of the word past the
//     inline slots, because the replay binds the value of each data slot
//     from Entry::scalar_values.
[[nodiscard]] CRUCIBLE_HOT constexpr bool entry_is_well_formed(const TraceRing::Entry& entry) noexcept {
    if (entry.schema_hash.raw() == 0) return false;
    if (entry.num_inputs > kMaxOpInputs) return false;
    if (entry.num_outputs > kMaxOpOutputs) return false;
    for (const ScalarCount stored : ScalarCount::check(entry.num_scalar_args)) {
        for (const DataSlots data : entry.data_slots())
            return data.first(stored) == data;
    }
    return false;
}

// The slots of an OpRecordStack.
using OpRecordSlots = ::foundation::core::WindowStack<TensorMeta, kMaxOpRecords>;

// The slots into which an adapter writes the records of the ops that run on
// the producer thread.  An op can run other ops before its records are
// complete: the boxed fallback of the vessel reads the inputs of an op, runs
// the op, and then reads its outputs, and the op can dispatch other ops.  So
// the slots are a stack.  An adapter opens one frame for each op through a
// frame door of the stack, and the frame holds its slots at the top of the
// stack until the arm of the door returns.  A frame that opens while another
// frame is open starts past the slots of that frame.  So an op never writes a
// record of an op that it runs inside.
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

    // The frame doors of the records of one op: the window doors of
    // foundation/core/WindowStack.h over the slots of the stack.
    // with_frame(count, on_open, on_closed) lends on_open the ViewMut of count
    // slots at the top, and the slots go back when on_open returns or raises.
    // When the slots do not fit, on_closed runs.  A frame that opens while
    // on_open runs, also the frame of an op that on_open runs, starts past
    // these slots.  with_frame<Count> lends a ViewMut of the fixed extent
    // Count.  O(1), and the cost of the arm.
    template <class OnOpen, class OnClosed>
    constexpr decltype(auto)
    with_frame(::foundation::core::WindowCount count, OnOpen&& on_open,
               OnClosed&& on_closed) & noexcept(noexcept(slots_.with_window(count, static_cast<OnOpen&&>(on_open),
                                                                            static_cast<OnClosed&&>(on_closed)))) {
        return slots_.with_window(count, static_cast<OnOpen&&>(on_open), static_cast<OnClosed&&>(on_closed));
    }
    template <uint32_t Count, class OnOpen, class OnClosed>
    constexpr decltype(auto) with_frame(OnOpen&& on_open, OnClosed&& on_closed) & noexcept(noexcept(
        slots_.template with_window<Count>(static_cast<OnOpen&&>(on_open), static_cast<OnClosed&&>(on_closed)))) {
        return slots_.template with_window<Count>(static_cast<OnOpen&&>(on_open), static_cast<OnClosed&&>(on_closed));
    }

    // The foreign frame doors: the frame doors above with no check of the
    // result and of the captures of the arms, for an arm that runs PyTorch
    // and gives its result (the foreign doors of WindowStack.h).  Each call
    // stands in an opt-out region of the class ABI, and so do these doors: they
    // give the result of an operator of PyTorch, such as a std::vector of
    // tensors.  O(1), and the cost of the arm.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a foreign frame gives the result of an operator of PyTorch")
    template <class OnOpen, class OnClosed>
    constexpr decltype(auto) with_foreign_frame(
        ::foundation::core::WindowCount count, OnOpen&& on_open,
        OnClosed&& on_closed) & noexcept(noexcept(slots_.with_foreign_window(count, static_cast<OnOpen&&>(on_open),
                                                                             static_cast<OnClosed&&>(on_closed)))) {
        return slots_.with_foreign_window(count, static_cast<OnOpen&&>(on_open), static_cast<OnClosed&&>(on_closed));
    }
    template <uint32_t Count, class OnOpen, class OnClosed>
    constexpr decltype(auto) with_foreign_frame(OnOpen&& on_open, OnClosed&& on_closed) & noexcept(
        noexcept(slots_.template with_foreign_window<Count>(static_cast<OnOpen&&>(on_open),
                                                            static_cast<OnClosed&&>(on_closed)))) {
        return slots_.template with_foreign_window<Count>(static_cast<OnOpen&&>(on_open),
                                                          static_cast<OnClosed&&>(on_closed));
    }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

private:
    friend class OpNest;

    // The slots and their top.  A frame door takes its slots through a window
    // door of the window stack, which opens only when its slots fit.
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

namespace detail {
class ValidatedOpDoor;
}  // namespace detail

// An entry that passed the trust ladder, with a View of its metadata
// records.  The count of the records is the size of the View, and it is the
// input count plus the output count of the entry.  The entry lives in the
// caller, so an op lives no longer than the call that made it.  The records
// live in the caller too, in a source that no code writes while the op lives
// (the doors below).  So an op lives no longer than the frame or the sealed
// list of its records.
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

// The sealed records of an op: a list that no code changes from its
// construction to its end (foundation/core/Sealed.h).  A Sealed does not
// copy, move or take an assignment, so a View of its list names the same
// records until the list ends.
template <std::size_t N>
using SealedRecords = ::fixy::Sealed<::fixy::FixedList<TensorMeta, N>>;

// The extent of a sealed list that the mint takes: at least one slot, and no
// more slots than the records that the ladder admits for one op.
template <std::size_t N>
concept OpRecordExtent = N > 0 && N <= kMaxOpRecords;

// The three doors to a ValidatedOp.  Each one gives no op when the count of
// the records is not the input count plus the output count of the entry.
//
//   mint_validated_op(entry, std::move(records))
//                       the records of the run of a frame.  The mint takes
//                       the ViewMut, the one writer of the run, by move.  So
//                       the name of the caller gives up its run, and no code
//                       writes the records while the frame lends them.  The
//                       op holds a View, so a checked frame refuses an op as
//                       its result (FrameResult of foundation/core/Choice.h)
//   mint_validated_op(entry, sealed)
//                       the records of a sealed list.  The op keeps a View of
//                       the list, so the op must not outlive the list.  The
//                       twin below refuses a temporary list.  No rule refuses
//                       an op that a function gives back past the end of a
//                       list that it holds as a local
//   mint_validated_op(entry)
//                       an op with no records
//
// A View of records gives no op: other code can hold a writer of the same
// records.  O(1).
template <::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr ::fixy::Option<ValidatedOp>
mint_validated_op(TraceRing::ValidatedEntryPtr entry, ::fixy::ViewMut<TensorMeta, Brand, Extent>&& records) noexcept;
template <std::size_t N>
    requires OpRecordExtent<N>
[[nodiscard]] constexpr ::fixy::Option<ValidatedOp>
mint_validated_op(TraceRing::ValidatedEntryPtr entry, SealedRecords<N> const& records CRUCIBLE_LIFETIMEBOUND) noexcept;
[[nodiscard]] constexpr ::fixy::Option<ValidatedOp> mint_validated_op(TraceRing::ValidatedEntryPtr entry) noexcept;

namespace detail {

// The key of the private constructor of ValidatedOp.  Its one member is
// private, and the three doors of mint_validated_op are its friends.  The
// return type of a mint names an Option of a ValidatedOp, and that Option
// needs a complete class.  So the class itself cannot name a mint as its
// friend.
class ValidatedOpDoor {
    // The op of an entry and its records, or no op when the count of the
    // records is not the input count plus the output count of the entry.
    [[nodiscard]] static constexpr ::fixy::Option<ValidatedOp> mint_(TraceRing::ValidatedEntryPtr entry,
                                                                     ::fixy::View<const TensorMeta> records) noexcept {
        CRUCIBLE_PRE(entry.value() != nullptr);
        const TraceRing::Entry& fields = *entry.value();
        const uint64_t expected = uint64_t{fields.num_inputs} + uint64_t{fields.num_outputs};
        if (records.size() != expected) return ::fixy::none;
        return ::fixy::Option<ValidatedOp>::some(ValidatedOp{entry, records});
    }

    template <::foundation::brand::IsBrand Brand, std::size_t Extent>
    friend constexpr ::fixy::Option<ValidatedOp>
    crucible::mint_validated_op(TraceRing::ValidatedEntryPtr entry,
                                ::fixy::ViewMut<TensorMeta, Brand, Extent>&& records) noexcept;
    template <std::size_t N>
        requires OpRecordExtent<N>
    friend constexpr ::fixy::Option<ValidatedOp> crucible::mint_validated_op(TraceRing::ValidatedEntryPtr entry,
                                                                             SealedRecords<N> const& records) noexcept;
    friend constexpr ::fixy::Option<ValidatedOp>
    crucible::mint_validated_op(TraceRing::ValidatedEntryPtr entry) noexcept;
};

}  // namespace detail

template <::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr ::fixy::Option<ValidatedOp>
mint_validated_op(TraceRing::ValidatedEntryPtr entry, ::fixy::ViewMut<TensorMeta, Brand, Extent>&& records) noexcept {
    ::fixy::ViewMut<TensorMeta, Brand, Extent> const held{std::move(records)};
    return detail::ValidatedOpDoor::mint_(entry, ::fixy::View<const TensorMeta>{held.view()});
}

template <std::size_t N>
    requires OpRecordExtent<N>
[[nodiscard]] constexpr ::fixy::Option<ValidatedOp>
mint_validated_op(TraceRing::ValidatedEntryPtr entry, SealedRecords<N> const& records CRUCIBLE_LIFETIMEBOUND) noexcept {
    return detail::ValidatedOpDoor::mint_(entry, records->view());
}

[[nodiscard]] constexpr ::fixy::Option<ValidatedOp> mint_validated_op(TraceRing::ValidatedEntryPtr entry) noexcept {
    return detail::ValidatedOpDoor::mint_(entry, ::fixy::View<const TensorMeta>{});
}

// The refused forms.  A named ViewMut stays the writer of the records with
// its caller, so the caller could write them after the mint.  A temporary
// sealed list ends before the op.  A View has no proof that no code writes
// its records.
template <class Brand, std::size_t Extent>
void mint_validated_op(TraceRing::ValidatedEntryPtr, ::fixy::ViewMut<TensorMeta, Brand, Extent>&) =
    delete("the mint takes the one writer of the records, so no name of the caller writes them after the mint: pass "
           "std::move(records)");
template <class Brand, std::size_t Extent>
void mint_validated_op(TraceRing::ValidatedEntryPtr, ::fixy::ViewMut<TensorMeta, Brand, Extent> const&) =
    delete("a const ViewMut stays the writer of the records with its holder: pass the ViewMut by move");
template <std::size_t N>
void mint_validated_op(TraceRing::ValidatedEntryPtr, SealedRecords<N> const&&) =
    delete("the op keeps a View of the sealed list, and a temporary list ends at the end of the full expression: "
           "seal the records under a name");
void mint_validated_op(TraceRing::ValidatedEntryPtr, ::fixy::View<const TensorMeta>) = delete(
    "a View has no proof that no code writes its records after the mint: give the ViewMut of a frame by move, "
    "or a Sealed list (SealedRecords)");

}  // namespace crucible
