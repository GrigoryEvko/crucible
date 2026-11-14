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
#include <foundation/contracts/Pre.h>

#include <cstdint>

namespace crucible {

// The most inputs and the most outputs of one op.  The trust ladder of the
// vessel refuses an entry with more of either (entry_is_well_formed), so the
// records of each op that the ladder admits fit kMaxOpRecords slots.
inline constexpr uint16_t kMaxOpInputs = 64;
inline constexpr uint16_t kMaxOpOutputs = 64;
inline constexpr uint32_t kMaxOpRecords = uint32_t{kMaxOpInputs} + uint32_t{kMaxOpOutputs};

// The slots into which an adapter decodes the records of one op before it
// builds the op.  Each slot holds a TensorMeta from the construction of the
// slots on.  A decode writes the slots from the first one and gives a View of
// the slots that it wrote, so a reader never reaches a slot of an earlier op.
using OpRecordSlots = ::fixy::FixedArray<TensorMeta, kMaxOpRecords>;

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
