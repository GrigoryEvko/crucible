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
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>
#include <fixy/Mutation.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible {

// A transaction holds its region by Ref only, so this header does not need
// the definition from MerkleDag.h.
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
    // The region of a commit: a Ref of a region in the arena, which is
    // never null.
    using ArenaRegion = ::fixy::Tagged<::fixy::Ref<RegionNode>, ::fixy::tags::source::Arena>;
    // The region of a transaction, or no region before its commit.
    using ArenaRegionLink = ::fixy::Tagged<::fixy::Option<::fixy::Ref<RegionNode>>, ::fixy::tags::source::Arena>;

    static_assert(sizeof(ArenaRegionLink) == sizeof(::fixy::Option<::fixy::Ref<RegionNode>>),
                  "the tag adds no byte to the link, and the empty link is the null pointer");

    // Replay determinism rests on this, so the type rejects a write that goes
    // backwards or wraps. Recycling a slot deliberately returns it to zero
    // before the new value is set: per-transaction monotonicity restarts with
    // the slot, and it is the log's own fill counter that keeps the sequence
    // of slots ordered.
    ::fixy::Monotonic<uint64_t> step_id = ::fixy::mint_monotonic<uint64_t>(0);
    // The place of the last displacement of this transaction in the order of
    // the displacements of its log, or zero when no activation displaced it.
    // A rollback restores the superseded transaction with the largest place,
    // so the order of the claims does not choose the rollback target.  A
    // restored transaction that the log displaces again gets a later place.
    ::fixy::Monotonic<uint64_t> displacement_order = ::fixy::mint_monotonic<uint64_t>(0);
    ContentHash content_hash;  // zero until the transaction commits
    MerkleHash merkle_root;  // zero until the transaction commits
    ArenaRegionLink region{};  // no region until the transaction commits
    // The place of the last event of this transaction in the order of the
    // events of its log: a claim, a commit, an activation, a displacement or
    // a rollback.  Zero until the log stamps the transaction.  The log keeps
    // the count, so two runs of one computation give the same stamps.  A time
    // for a person goes to the sink of fixy/os/TimingSink.h, which only a
    // context whose row holds Nondet reads.
    ::fixy::Monotonic<uint64_t> event_order = ::fixy::mint_monotonic<uint64_t>(0);
    TxStatus status = TxStatus::RECORDING;
    ::fixy::FixedArray<uint8_t, 15> pad{};
};

// True when the transaction holds the region.  The test compares the
// addresses of the two regions, so a copy of the region is not the region.
// A transaction with no region holds no region.
[[nodiscard]] inline bool holds_region(Transaction const& tx, ::fixy::Ref<const RegionNode> region) noexcept {
    return tx.region.value().is_some_and(
        [region](::fixy::Ref<RegionNode> const& held) noexcept { return ::fixy::same_object(held, region); });
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

// The log stamps each event of a transaction with the next place of its own
// count, and it reads no clock.  A clock time in a log makes two runs of one
// computation give two logs.  So no method of the log takes the tag of
// Nondet, and a body whose context holds no Nondet can roll the log back.
//
// A transaction that the log gives, by reference or as a WeakRef, stays
// valid for as long as the ring has not wrapped past its slot.  It is the
// owner's to use: the proof admits the call, and the transaction must stay
// on the owning thread.  A claim never recycles the slot of the active
// transaction or the slot of the rollback target.  Each of the two stays
// valid while the transaction keeps its role.  A claim does not change the
// rollback target.

template <uint32_t N, OwnerProof Owner>
class TransactionLog {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    // A claim in a full ring keeps two slots: the slot of the active
    // transaction and the slot of the rollback target.  The ring must hold
    // a third slot for the claim.  N is a power of 2, and the smallest such
    // ring holds 4 slots.  With the fourth slot, a claim also never recycles
    // the slot of the last claim.
    static_assert(N >= 4,
                  "N must be at least 4: a claim keeps the slots of the active transaction and the rollback target, "
                  "and N is a power of 2");

public:
    // The slots, the write cursor and the fill counter as one composition, so
    // the wrap masking and the saturating fill live in the type rather than
    // being open-coded at each use. The fill saturates at the capacity instead
    // of failing, and never moves backwards.
    using Ring = ::fixy::CyclicBuffer<Transaction, N>;

    TransactionLog() noexcept = default;

    TransactionLog(const TransactionLog&) = delete("TransactionLog holds ring-internal pointers");
    TransactionLog& operator=(const TransactionLog&) = delete("TransactionLog holds ring-internal pointers");
    TransactionLog(TransactionLog&&) = delete("interior pointers into entries_ would dangle");
    TransactionLog& operator=(TransactionLog&&) = delete("interior pointers into entries_ would dangle");

    [[nodiscard, gnu::cold]] Transaction& begin_tx(Owner const& owner, uint64_t step_id) noexcept {
        // A full ring claims its oldest slot, but two slots must stay.  A
        // rollback can make a transaction of any age the active one.  If a
        // claim recycles that slot, the live transaction ends.  Then activate
        // finds the new transaction in the active slot.  An activation out of
        // order, or a rollback before an activation, can put the rollback
        // target in any slot.  If a claim recycles that slot, a rollback
        // restores an older transaction.
        const ::fixy::WeakRef<Transaction> target = previous(owner);
        // A pass that finds a kept slot as the oldest claims that slot, which
        // makes it the newest entry.  The claim keeps two slots, and the ring
        // holds at least 4.  After two passes, the oldest slot holds neither.
        for (uint32_t pass = 0; pass < 2 && ring_.full(); ++pass) {
            const ::fixy::WeakRef<Transaction> oldest{ring_.recent(N - 1)};
            if (oldest != active_ref_() && oldest != target) break;
            (void)ring_.claim();
        }
        // The fresh claim ends the life of the old transaction and starts a
        // new one in the slot.  An assignment would write step_id backward,
        // and its Monotonic refuses an assignment.  The value initialization
        // also sets each padding byte to zero.
        Transaction& tx = ring_.claim_fresh();
        // Set rather than assigned, so a later edit cannot reintroduce a write
        // that goes backwards.
        tx.step_id.advance(step_id);
        stamp_(tx);
        // A contract predicate reads each local as const, so the borrow of
        // tx comes before the clauses.
        const ::fixy::WeakRef<Transaction> claimed{tx};
        CRUCIBLE_POST(claimed, claimed != active_ref_());
        CRUCIBLE_POST(claimed, claimed != target);
        CRUCIBLE_POST(claimed, tx.status == TxStatus::RECORDING);
        CRUCIBLE_POST(claimed, tx.step_id.get() == step_id);
        CRUCIBLE_POST(claimed, tx.event_order.get() == events_.get());
        return tx;
    }

    // Returns false when the transaction is not in a state it can commit from.
    //
    // A zero root is refused. Zero is what a subtree hash reads before it is
    // computed, and it is also what a freshly recycled slot holds, so a
    // committed entry carrying zero is indistinguishable from an uncommitted
    // one and any search over the log becomes ambiguous. Refusing it here also
    // catches the case where the commit runs before the hash was recomputed.
    [[nodiscard]] bool commit(Owner const&, Transaction& tx, Transaction::ArenaRegion region, ContentHash content_hash,
                              MerkleHash merkle_root) noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(merkle_root));
        if (tx.status != TxStatus::RECORDING && tx.status != TxStatus::CLOSED) {
            CRUCIBLE_POST(0, !false || tx.status == TxStatus::COMMITTED);
            return false;
        }
        // The region came in under the Arena tag, so the link keeps it.
        tx.region = ::fixy::mint_tagged<::fixy::tags::source::Arena>(
            ::fixy::Option<::fixy::Ref<RegionNode>>::some(region.value()));
        tx.content_hash = content_hash;
        tx.merkle_root = merkle_root;
        tx.status = TxStatus::COMMITTED;
        stamp_(tx);
        // Dropping any one of these assignments leaves the field at the value
        // the slot was reset to, which reads as an uncommitted transaction.
        CRUCIBLE_POST(true, tx.status == TxStatus::COMMITTED);
        CRUCIBLE_POST(true, holds_region(tx, region.value()));
        CRUCIBLE_POST(true, tx.content_hash == content_hash);
        CRUCIBLE_POST(true, tx.merkle_root == merkle_root);
        return true;
    }

    // Returns the transaction this one displaces, which is the target a
    // rollback would restore, or an empty reference if there was none.
    [[nodiscard]] ::fixy::WeakRef<Transaction> activate(Owner const&, Transaction& tx) noexcept {
        if (tx.status != TxStatus::COMMITTED) {
            CRUCIBLE_POST(0, true || tx.status == TxStatus::ACTIVE);
            return ::fixy::WeakRef<Transaction>{};
        }

        const ::fixy::WeakRef<Transaction> prev = active_ref_();
        if (prev.has_value()) {
            prev->status = TxStatus::SUPERSEDED;
            displacements_.bump();
            prev->displacement_order.advance(displacements_.get());
            stamp_(*prev);
        }

        tx.status = TxStatus::ACTIVE;
        stamp_(tx);
        active_tx_ = ::fixy::mint_tagged<::fixy::tags::source::Ring>(&tx);
        // The displaced transaction must end up in the superseded state, since
        // that is what a rollback searches for.  It must also hold the newest
        // place in the order of displacements, because a rollback restores
        // the superseded transaction with the largest place.
        //
        // The last two clauses are written as disjunctions rather than through
        // the named implication predicate, because that predicate is an
        // ordinary call and evaluates both of its arguments. With an empty
        // antecedent the consequent would read through the empty reference
        // before the predicate could fold.
        CRUCIBLE_POST(prev, tx.status == TxStatus::ACTIVE);
        CRUCIBLE_POST(prev, active_tx_.value() == &tx);
        CRUCIBLE_POST(prev, !prev.has_value() || prev->status == TxStatus::SUPERSEDED);
        CRUCIBLE_POST(prev, !prev.has_value() || prev->displacement_order.get() == displacements_.get());
        return prev;
    }

    // Restores the most recently displaced transaction and marks the current
    // one rolled back. Returns false when there is nothing to restore.
    [[nodiscard]] bool rollback(Owner const& owner) noexcept {
        const ::fixy::WeakRef<Transaction> prev = previous(owner);
        if (!prev.has_value()) return false;

        const ::fixy::WeakRef<Transaction> current = active_ref_();
        if (current.has_value()) {
            current->status = TxStatus::ROLLED_BACK;
            stamp_(*current);
        }
        prev->status = TxStatus::ACTIVE;
        stamp_(*prev);
        active_tx_ = ::fixy::mint_tagged<::fixy::tags::source::Ring>(prev.try_get());
        return true;
    }

    // Not const: the caller may need to change the transaction it returns.
    // The reference is empty when no transaction is active.
    //
    // The transaction is a slot of the ring, and the ring is a member of this
    // log.  A temporary log releases the ring at the end of the statement.  A
    // deleted `&&` twin refuses each call on a temporary log, here and in
    // previous().
    [[nodiscard]] ::fixy::WeakRef<Transaction> active(Owner const&) & { return active_ref_(); }
    ::fixy::WeakRef<Transaction> active(Owner const&) && =
        delete("a temporary log releases its ring at the end of the statement. Use a log that has a name");

    // Returns the rollback target, or an empty reference when no transaction
    // in the ring is superseded.  The target is the superseded transaction
    // with the newest displacement.  A caller can activate an older
    // transaction after a newer one, so the order of the claims does not give
    // the target.  The scan is O(N) in the ring capacity.
    [[nodiscard]] ::fixy::WeakRef<Transaction> previous(Owner const&) & {
        ::fixy::WeakRef<Transaction> target{};
        for (typename Ring::size_type i = 0; i < ring_.size(); i++) {
            Transaction& entry = ring_.recent(i);
            const bool is_newer_displacement =
                entry.status == TxStatus::SUPERSEDED
                && (!target.has_value() || entry.displacement_order.get() > target->displacement_order.get());
            if (!is_newer_displacement) continue;
            target = ::fixy::WeakRef<Transaction>{entry};
            // No place is larger than the count of displacements.  When no
            // rollback undid the last displacement, the scan stops at its
            // transaction.  In a log that begins, commits and activates each
            // transaction in sequence, that transaction is in the second
            // newest slot.
            if (entry.displacement_order.get() == displacements_.get()) break;
        }
        // Each displacement takes a new place, so no two superseded
        // transactions share a place, and the scan order cannot change the
        // result.
        CRUCIBLE_POST(target, !target.has_value() || target->status == TxStatus::SUPERSEDED);
        CRUCIBLE_POST(target, !target.has_value() || target->displacement_order.get() != 0);
        return target;
    }

    ::fixy::WeakRef<Transaction> previous(Owner const&) && =
        delete("a temporary log releases its ring at the end of the statement. Use a log that has a name");

    // Gives the region of each transaction in the ring to visit.  A rollback
    // can make each such transaction active again, so the owner keeps each of
    // these regions.  Complexity: O(N) in the ring capacity.
    template <class Visit>
    void for_each_region(Owner const&, Visit&& visit) const {
        for (typename Ring::size_type i = 0; i < ring_.size(); i++) {
            const Transaction& entry = ring_.recent(i);
            for (const ::fixy::Ref<RegionNode>& region : entry.region.value())
                visit(::fixy::WeakRef<const RegionNode>{*region});
        }
    }

    [[nodiscard]] uint32_t size(Owner const&) const {
        // The fill saturates at the capacity, which is far below what the
        // narrower type holds, so the conversion loses nothing.
        return static_cast<uint32_t>(ring_.size());
    }

private:
    // Gives the transaction the next place of the count of the events.  The
    // count only grows, so a later event of one transaction gets a larger
    // place.  A slot that a claim recycles starts again at zero.
    void stamp_(Transaction& tx) noexcept {
        events_.bump();
        tx.event_order.advance(events_.get());
    }

    // The live-slot pointer is not part of the ring: the ring's own cursor
    // says where the next write goes, and this says which past slot is
    // current. The log cannot be copied or moved, so the ring never relocates
    // and this pointer stays valid for the log's lifetime. Its tag records
    // that it came from a fixed-capacity ring's inline storage, which has a
    // different lifetime from an arena-owned or borrowed pointer.
    using ActiveTxPtr = ::fixy::Tagged<Transaction*, ::fixy::tags::source::Ring>;
    static_assert(sizeof(ActiveTxPtr) == sizeof(Transaction*) && alignof(ActiveTxPtr) == alignof(Transaction*),
                  "a tagged transaction pointer must keep the layout of the pointer it wraps");

    // The active transaction as a borrow, or an empty one when no
    // transaction is active.  The slot is in the ring of this log.
    [[nodiscard]] ::fixy::WeakRef<Transaction> active_ref_() const noexcept {
        return ::fixy::WeakRef<Transaction>::from_raw(active_tx_.value());
    }

    Ring ring_{};
    ActiveTxPtr active_tx_ = ::fixy::mint_tagged<::fixy::tags::source::Ring, Transaction*>(nullptr);
    // The count of displacements.  Each activation that displaces a
    // transaction advances it and gives the new value to the displaced
    // transaction as its place.
    ::fixy::Monotonic<uint64_t> displacements_ = ::fixy::mint_monotonic<uint64_t>(0);
    // The count of the events of the log.  Each stamp advances it and gives
    // the new value to the transaction of the event.
    ::fixy::Monotonic<uint64_t> events_ = ::fixy::mint_monotonic<uint64_t>(0);
};

}  // namespace crucible
