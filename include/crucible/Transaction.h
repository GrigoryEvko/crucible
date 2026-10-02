#pragma once

// One iteration is one transaction. It records, then commits once its region
// is built and hashed, then becomes the live one. The transaction it displaced
// is kept rather than discarded, so a regression can restore it.
//
// One thread owns the log, and every state read here is a plain read for that
// reason. The type holds that rule instead of a comment: each member takes a
// proof of type Owner, and only the owning thread can hold one. A second
// thread that wants a change sends it to the owner as a message.

#include <crucible/Types.h>
#include <fixy/CyclicBuffer.h>
#include <fixy/Mutation.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/os/ClockSource.h>
#include <fixy/os/Time.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <system_error>
#include <type_traits>

namespace crucible {

// A transaction holds its region by pointer only, so this header does not
// need the definition from MerkleDag.h.
struct RegionNode;

enum class TxStatus : uint8_t {
    RECORDING,  // the ring is accepting operations for this transaction
    CLOSED,  // an iteration boundary was reached and the ring is sealed
    COMMITTED,  // the region is built and its hashes are computed
    ACTIVE,  // this region is the live one
    SUPERSEDED,  // a newer transaction took over, but this one is kept
    ROLLED_BACK,  // quality regressed and the previous transaction was restored
};

struct Transaction {
    using ArenaRegion = ::fixy::Tagged<RegionNode*, ::fixy::tags::source::Arena>;

    // The type pins which clock this reading came from. A wall clock jumps
    // when the system time is corrected, and a boot clock counts time spent
    // suspended. Either would make the ordering of these stamps meaningless.
    using Timestamp = ::fixy::MonotonicClockBytes<std::uint64_t>;

    static_assert(sizeof(ArenaRegion) == sizeof(RegionNode*));
    static_assert(sizeof(Timestamp) == sizeof(std::uint64_t),
                  "a clock-source-tagged timestamp must be the size of the value it wraps");

    // Replay determinism rests on this, so the type rejects a write that goes
    // backwards or wraps. Recycling a slot deliberately returns it to zero
    // before the new value is set: per-transaction monotonicity restarts with
    // the slot, and it is the log's own fill counter that keeps the sequence
    // of slots ordered.
    ::fixy::Monotonic<uint64_t> step_id = ::fixy::mint_monotonic<uint64_t>(0);
    ContentHash content_hash;  // zero until the transaction commits
    MerkleHash merkle_root;  // zero until the transaction commits
    ArenaRegion region = ::fixy::mint_tagged<::fixy::tags::source::Arena, RegionNode*>(nullptr);
    // Empty until the log stamps the transaction, and empty after a stamp
    // whose clock read failed.  Only a clock reader builds a reading, so a
    // slot that the log never claimed holds none, and no default value can
    // claim a time that no clock returned.
    std::optional<Timestamp> ts_ns;
    TxStatus status = TxStatus::RECORDING;
    uint8_t pad[7]{};
};

// Stores a clock reading in a transaction.  A failed read leaves the
// transaction with no reading: no value stands for a failed read, and a
// reading of an earlier event does not stay to claim the time of this
// one.
inline void stamp_transaction(Transaction& tx,
                              std::expected<Transaction::Timestamp, std::error_code> const& reading) noexcept {
    if (reading) {
        tx.ts_ns = *reading;
    } else {
        tx.ts_ns.reset();
    }
    CRUCIBLE_POST(true, tx.ts_ns.has_value() == reading.has_value());
}

// A proof that the caller is the one thread that owns some state. It is
// empty, so it costs nothing at a call. It cannot be copied or moved, so a
// thread cannot hand it to another thread by value. It has no public default
// constructor, so only its owner can build one. It is not trivially copyable
// and not an implicit-lifetime type, so no bit_cast and no lifetime start over
// bytes can build one either.
//
// This is the ownership half of concurrent separation logic (O'Hearn,
// Resources, Concurrency and Local Reasoning, 2007): one thread owns the
// resource, and the frame rule keeps every other thread out of it.
//
// GCC counts a class whose copy and move are all deleted as trivially
// copyable, so an owner type also needs a destructor that is not trivial.
// An empty user-provided destructor is enough and costs nothing.
template <typename P>
concept OwnerProof =
    std::is_empty_v<P> && !std::is_copy_constructible_v<P> && !std::is_move_constructible_v<P>
    && !std::is_default_constructible_v<P> && !std::is_trivially_copyable_v<P> && !std::is_implicit_lifetime_v<P>;

// The log stamps each transaction with the monotonic clock, and a clock
// reader is minted only by a context that owns Bg, Init or Test.  So the
// log is built only from such a context.  The foreground dispatch path is
// replay-bound and reads no clock.
template <typename Ctx>
concept CtxFitsTransactionLog = ::fixy::time::CtxFitsClockReaderMint<Ctx, ::fixy::ClockSource_v::Monotonic>;

// A pointer the log returns stays valid for as long as the ring has not
// wrapped past the slot it points into, and it is the owner's to use: the
// proof admits the call, and the pointer must stay on the owning thread.
// A claim never recycles the slot of the active transaction, so a pointer to
// the active transaction stays valid while that transaction is active.

template <uint32_t N, OwnerProof Owner>
class TransactionLog {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    static_assert(N != 1, "the ring must hold the active transaction and the transaction that a claim builds");

public:
    // The slots, the write cursor and the fill counter as one composition, so
    // the wrap masking and the saturating fill live in the type rather than
    // being open-coded at each use. The fill saturates at the capacity instead
    // of failing, and never moves backwards.
    using Ring = ::fixy::CyclicBuffer<Transaction, N>;

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsTransactionLog<Ctx>
    explicit TransactionLog(Ctx const& ctx) noexcept
        : clock_{::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx)} {}

    TransactionLog(const TransactionLog&) = delete("TransactionLog holds ring-internal pointers");
    TransactionLog& operator=(const TransactionLog&) = delete("TransactionLog holds ring-internal pointers");
    TransactionLog(TransactionLog&&) = delete("interior pointers into entries_ would dangle");
    TransactionLog& operator=(TransactionLog&&) = delete("interior pointers into entries_ would dangle");

    [[nodiscard, gnu::cold]] Transaction* begin_tx(Owner const&, uint64_t step_id) noexcept {
        // A full ring claims its oldest slot.  A rollback can make a
        // transaction of any age the active one, so the oldest slot can hold
        // the active transaction.  A recycle of that slot ends the life of
        // the live transaction, and activate then finds the new transaction
        // in the active slot.  So the claim passes over the active slot.  The
        // active transaction becomes the newest entry, and the claim below
        // takes the slot of the oldest entry other than the active one.
        if (ring_.full() && &ring_.recent(N - 1) == active_tx_.value()) {
            (void)ring_.claim();
        }
        // Claiming advances only the cursor, so the reference it hands back
        // stays valid across the reset that follows.
        Transaction* tx = &ring_.claim();
        // Recycling ends the life of the old transaction and starts a new
        // one in the slot.  An assignment would write step_id backward, and
        // its Monotonic refuses an assignment.
        std::destroy_at(tx);
        std::construct_at(tx);
        // Set rather than assigned, so a later edit cannot reintroduce a write
        // that goes backwards.
        tx->step_id.advance(step_id);
        stamp_transaction(*tx, clock_.read());
        // Reordering the claim and the reset would return a pointer into a
        // slot still holding the previous transaction.  A failed clock read
        // leaves the timestamp empty, so the postcondition of
        // stamp_transaction states the timestamp, and none here does.
        CRUCIBLE_POST(tx, tx != nullptr);
        CRUCIBLE_POST(tx, tx != active_tx_.value());
        CRUCIBLE_POST(tx, tx->status == TxStatus::RECORDING);
        CRUCIBLE_POST(tx, tx->step_id.get() == step_id);
        return tx;
    }

    // Returns false when the transaction is not in a state it can commit from.
    //
    // A zero root is refused. Zero is what a subtree hash reads before it is
    // computed, and it is also what a freshly recycled slot holds, so a
    // committed entry carrying zero is indistinguishable from an uncommitted
    // one and any search over the log becomes ambiguous. Refusing it here also
    // catches the case where the commit runs before the hash was recomputed.
    [[nodiscard]] bool commit(Owner const&, Transaction* const tx, Transaction::ArenaRegion region,
                              ContentHash content_hash, MerkleHash merkle_root) noexcept {
        CRUCIBLE_PRE(tx != nullptr);
        CRUCIBLE_PRE(region.value() != nullptr);
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(merkle_root));
        if (tx->status != TxStatus::RECORDING && tx->status != TxStatus::CLOSED) {
            CRUCIBLE_POST(0, !false || tx->status == TxStatus::COMMITTED);
            return false;
        }
        tx->region = region;
        tx->content_hash = content_hash;
        tx->merkle_root = merkle_root;
        tx->status = TxStatus::COMMITTED;
        stamp_transaction(*tx, clock_.read());
        // Dropping any one of these assignments leaves the field at the value
        // the slot was reset to, which reads as an uncommitted transaction.
        CRUCIBLE_POST(true, tx->status == TxStatus::COMMITTED);
        CRUCIBLE_POST(true, tx->region.value() == region.value());
        CRUCIBLE_POST(true, tx->content_hash == content_hash);
        CRUCIBLE_POST(true, tx->merkle_root == merkle_root);
        return true;
    }

    // Returns the transaction this one displaces, which is the target a
    // rollback would restore, or null if there was none.
    [[nodiscard]] Transaction* activate(Owner const&, Transaction* const tx) noexcept {
        CRUCIBLE_PRE(tx != nullptr);
        if (tx->status != TxStatus::COMMITTED) {
            CRUCIBLE_POST(static_cast<Transaction*>(nullptr), true || tx->status == TxStatus::ACTIVE);
            return nullptr;
        }

        Transaction* prev = nullptr;
        if (active_tx_.value() != nullptr) {
            active_tx_.value()->status = TxStatus::SUPERSEDED;
            stamp_transaction(*active_tx_.value(), clock_.read());
            prev = active_tx_.value();
        }

        tx->status = TxStatus::ACTIVE;
        stamp_transaction(*tx, clock_.read());
        active_tx_ = ::fixy::mint_tagged<::fixy::tags::source::Ring>(tx);
        // The displaced transaction must end up in the superseded state, since
        // that is what a rollback searches for.
        //
        // The last clause is written as a disjunction rather than through the
        // named implication predicate, because that predicate is an ordinary
        // call and evaluates both of its arguments. With a null antecedent the
        // consequent would dereference null before the predicate could fold.
        CRUCIBLE_POST(prev, tx->status == TxStatus::ACTIVE);
        CRUCIBLE_POST(prev, active_tx_.value() == tx);
        CRUCIBLE_POST(prev, prev == nullptr || prev->status == TxStatus::SUPERSEDED);
        return prev;
    }

    // Restores the most recently displaced transaction and marks the current
    // one rolled back. Returns false when there is nothing to restore.
    [[nodiscard]] bool rollback(Owner const& owner) noexcept {
        Transaction* prev = previous(owner);
        if (!prev) return false;

        if (active_tx_.value() != nullptr) {
            active_tx_.value()->status = TxStatus::ROLLED_BACK;
            stamp_transaction(*active_tx_.value(), clock_.read());
        }
        prev->status = TxStatus::ACTIVE;
        stamp_transaction(*prev, clock_.read());
        active_tx_ = ::fixy::mint_tagged<::fixy::tags::source::Ring>(prev);
        return true;
    }

    // Not const: the caller may need to change the transaction it returns.
    [[nodiscard]] Transaction* active(Owner const&) CRUCIBLE_LIFETIMEBOUND { return active_tx_.value(); }

    [[nodiscard]] Transaction* previous(Owner const&) CRUCIBLE_LIFETIMEBOUND {
        // Walks back from the most recently claimed slot, so the first
        // displaced transaction it finds is the newest one.
        for (typename Ring::size_type i = 0; i < ring_.size(); i++) {
            Transaction& e = ring_.recent(i);
            if (e.status == TxStatus::SUPERSEDED) return &e;
        }
        return nullptr;
    }

    [[nodiscard]] uint32_t size(Owner const&) const {
        // The fill saturates at the capacity, which is far below what the
        // narrower type holds, so the conversion loses nothing.
        return static_cast<uint32_t>(ring_.size());
    }

private:
    static_assert(std::is_same_v<decltype(std::declval<::fixy::time::MonotonicClock const&>().read()),
                                 std::expected<Transaction::Timestamp, std::error_code>>,
                  "the log's clock reader must return the reading a transaction stores, or the error of a failed read");

    // The live-slot pointer is not part of the ring: the ring's own cursor
    // says where the next write goes, and this says which past slot is
    // current. The log cannot be copied or moved, so the ring never relocates
    // and this pointer stays valid for the log's lifetime. Its tag records
    // that it came from a fixed-capacity ring's inline storage, which has a
    // different lifetime from an arena-owned or borrowed pointer.
    using ActiveTxPtr = ::fixy::Tagged<Transaction*, ::fixy::tags::source::Ring>;
    static_assert(sizeof(ActiveTxPtr) == sizeof(Transaction*) && alignof(ActiveTxPtr) == alignof(Transaction*),
                  "a tagged transaction pointer must keep the layout of the pointer it wraps");

    Ring ring_{};
    ActiveTxPtr active_tx_ = ::fixy::mint_tagged<::fixy::tags::source::Ring, Transaction*>(nullptr);
    // The reader clamps its readings, so two stamps through it never
    // regress even when the clock steps backward.
    ::fixy::time::MonotonicClock clock_;
};

}  // namespace crucible
