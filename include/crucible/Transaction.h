#pragma once

// One iteration is one transaction. It records, then commits once its region
// is built and hashed, then becomes the live one. The transaction it displaced
// is kept rather than discarded, so a regression can restore it.
//
// The log is owned by a single writing thread, and every state read here is a
// plain read for that reason.

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/_ClockSource.h>
#include <crucible/safety/_Decide.h>
#include <crucible/safety/_Post.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace crucible {

enum class TxStatus : uint8_t {
    RECORDING,  // the ring is accepting operations for this transaction
    CLOSED,  // an iteration boundary was reached and the ring is sealed
    COMMITTED,  // the region is built and its hashes are computed
    ACTIVE,  // this region is the live one
    SUPERSEDED,  // a newer transaction took over, but this one is kept
    ROLLED_BACK,  // quality regressed and the previous transaction was restored
};

struct Transaction {
    using ArenaRegion = ::crucible::fixy::wrap::Tagged<RegionNode*, ::crucible::fixy::tags::source::Arena>;

    static_assert(sizeof(ArenaRegion) == sizeof(RegionNode*));

    // Replay determinism rests on this, so the type rejects a write that goes
    // backwards or wraps. Recycling a slot deliberately returns it to zero
    // before the new value is set: per-transaction monotonicity restarts with
    // the slot, and it is the log's own fill counter that keeps the sequence
    // of slots ordered.
    ::crucible::fixy::wrap::Monotonic<uint64_t> step_id{0};
    ContentHash content_hash;  // zero until the transaction commits
    MerkleHash merkle_root;  // zero until the transaction commits
    ArenaRegion region{nullptr};  // null until the transaction commits
    // The type pins which clock this reading came from. A wall clock jumps
    // when the system time is corrected, and a boot clock counts time spent
    // suspended. Either would make the ordering of these stamps meaningless.
    ::crucible::safety::MonotonicClockBytes<std::uint64_t> ts_ns{};
    TxStatus status = TxStatus::RECORDING;
    uint8_t pad[7]{};
};

static_assert(sizeof(Transaction) == 48, "Transaction layout must be 48 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Transaction);

static_assert(
    std::is_same_v<decltype(std::declval<Transaction>().ts_ns), ::crucible::safety::MonotonicClockBytes<std::uint64_t>>,
    "a transaction timestamp must carry its monotonic-clock provenance");
static_assert(sizeof(::crucible::safety::MonotonicClockBytes<std::uint64_t>) == sizeof(std::uint64_t),
              "a clock-source-tagged timestamp must be the size of the value it wraps");

// Not thread-safe: one thread writes it. A pointer it returns stays valid for
// as long as the ring has not wrapped past the slot it points into.

template <uint32_t N = 16>
class TransactionLog {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");

public:
    // The slots, the write cursor and the fill counter as one composition, so
    // the wrap masking and the saturating fill live in the type rather than
    // being open-coded at each use. The fill saturates at the capacity instead
    // of failing, and never moves backwards.
    using Ring = ::crucible::fixy::wrap::CyclicBuffer<Transaction, N>;

    TransactionLog() = default;
    TransactionLog(const TransactionLog&) = delete("TransactionLog holds ring-internal pointers");
    TransactionLog& operator=(const TransactionLog&) = delete("TransactionLog holds ring-internal pointers");
    TransactionLog(TransactionLog&&) = delete("interior pointers into entries_ would dangle");
    TransactionLog& operator=(TransactionLog&&) = delete("interior pointers into entries_ would dangle");

    [[nodiscard, gnu::cold]] Transaction* begin_tx(uint64_t step_id) noexcept {
        // Claiming advances only the cursor, so the reference it hands back
        // stays valid across the reset that follows.
        Transaction* tx = &ring_.claim();
        *tx = Transaction{};
        // Set rather than assigned, so a later edit cannot reintroduce a write
        // that goes backwards.
        tx->step_id.advance(step_id);
        tx->ts_ns = now_ns();
        // Reordering the claim and the reset would return a pointer into a
        // slot still holding the previous transaction.
        CRUCIBLE_POST(tx, tx != nullptr);
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
    [[nodiscard]] bool commit(Transaction* const tx, Transaction::ArenaRegion region, ContentHash content_hash,
                              MerkleHash merkle_root) noexcept pre(tx != nullptr) pre(region.value() != nullptr)
        pre(::crucible::decide::is_non_zero(merkle_root)) {
        // A contract predicate that reads through a parameter's pointee is
        // skipped when the compiler folds the body at compile time, so every
        // such check in this file runs from the body rather than a clause.
        if (tx->status != TxStatus::RECORDING && tx->status != TxStatus::CLOSED) {
            CRUCIBLE_POST(0, !false || tx->status == TxStatus::COMMITTED);
            return false;
        }
        tx->region = region;
        tx->content_hash = content_hash;
        tx->merkle_root = merkle_root;
        tx->status = TxStatus::COMMITTED;
        tx->ts_ns = now_ns();
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
    [[nodiscard]] Transaction* activate(Transaction* const tx) noexcept pre(tx != nullptr) {
        if (tx->status != TxStatus::COMMITTED) {
            CRUCIBLE_POST(static_cast<Transaction*>(nullptr), true || tx->status == TxStatus::ACTIVE);
            return nullptr;
        }

        Transaction* prev = nullptr;
        if (active_tx_.value() != nullptr) {
            active_tx_.value()->status = TxStatus::SUPERSEDED;
            active_tx_.value()->ts_ns = now_ns();
            prev = active_tx_.value();
        }

        tx->status = TxStatus::ACTIVE;
        tx->ts_ns = now_ns();
        active_tx_ = ActiveTxPtr{tx};
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
    [[nodiscard]] bool rollback() noexcept {
        Transaction* prev = previous();
        if (!prev) return false;

        if (active_tx_.value() != nullptr) {
            active_tx_.value()->status = TxStatus::ROLLED_BACK;
            active_tx_.value()->ts_ns = now_ns();
        }
        prev->status = TxStatus::ACTIVE;
        prev->ts_ns = now_ns();
        active_tx_ = ActiveTxPtr{prev};
        return true;
    }

    // Not const: the caller may need to change the transaction it returns.
    [[nodiscard]] Transaction* active() CRUCIBLE_LIFETIMEBOUND { return active_tx_.value(); }

    [[nodiscard]] Transaction* previous() CRUCIBLE_LIFETIMEBOUND {
        // Walks back from the most recently claimed slot, so the first
        // displaced transaction it finds is the newest one.
        for (typename Ring::size_type i = 0; i < ring_.size(); i++) {
            Transaction& e = ring_.recent(i);
            if (e.status == TxStatus::SUPERSEDED) return &e;
        }
        return nullptr;
    }

    [[nodiscard]] uint32_t size() const {
        // The fill saturates at the capacity, which is far below what the
        // narrower type holds, so the conversion loses nothing.
        return static_cast<uint32_t>(ring_.size());
    }

private:
    // The steady clock is the monotonic reading on the supported platforms.
    // The mint only stamps that provenance onto it, so a stamp taken from any
    // other clock cannot be assigned into a transaction.
    [[nodiscard]] static auto now_ns() noexcept -> ::crucible::safety::MonotonicClockBytes<std::uint64_t> {
        const auto tp = std::chrono::steady_clock::now();
        const std::uint64_t raw = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count());
        return ::crucible::safety::mint_clock_source<::crucible::safety::ClockSource_v::Monotonic, std::uint64_t>(raw);
    }

    static_assert(std::is_same_v<decltype(now_ns()), ::crucible::safety::MonotonicClockBytes<std::uint64_t>>,
                  "the timestamp source must return a monotonic-clock reading");
    static_assert(sizeof(decltype(now_ns())) == sizeof(std::uint64_t),
                  "a clock-source-tagged timestamp must be the size of the value it wraps");

    // The live-slot pointer is not part of the ring: the ring's own cursor
    // says where the next write goes, and this says which past slot is
    // current. The log cannot be copied or moved, so the ring never relocates
    // and this pointer stays valid for the log's lifetime. Its tag records
    // that it came from a fixed-capacity ring's inline storage, which has a
    // different lifetime from an arena-owned or borrowed pointer.
    using ActiveTxPtr = ::crucible::safety::Tagged<Transaction*, ::crucible::safety::source::Ring>;
    Ring ring_{};
    ActiveTxPtr active_tx_{nullptr};
};

// The ring composition adds nothing beyond its three members.
static_assert(sizeof(::crucible::fixy::wrap::CyclicBuffer<Transaction, 16>)
                  == sizeof(::crucible::safety::FixedArray<Transaction, 16>)
                         + sizeof(::crucible::safety::Cyclic<std::size_t, 16>)
                         + sizeof(::crucible::safety::BoundedMonotonic<std::size_t, 16>),
              "CyclicBuffer<Transaction, N> must stay a zero-overhead composition");

// The tag must not add storage, or the whole log grows.
static_assert(sizeof(::crucible::safety::Tagged<Transaction*, ::crucible::safety::source::Ring>)
                  == sizeof(Transaction*),
              "a tagged transaction pointer must be the size of the pointer it wraps");
static_assert(alignof(::crucible::safety::Tagged<Transaction*, ::crucible::safety::source::Ring>)
                  == alignof(Transaction*),
              "a tagged transaction pointer must be aligned as the pointer it wraps");

}  // namespace crucible
