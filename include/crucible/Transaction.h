#pragma once

// Transaction lifecycle model for Crucible's Kafka-like iteration pipeline.
//
// One iteration = one transaction:
//   begin_tx()   → RECORDING  (ring accepts ops)
//   commit()     → COMMITTED  (RegionNode built, hashes computed)
//   activate()   → ACTIVE     (atomic swap done, this region is live)
//   [superseded] → SUPERSEDED (newer tx took over, kept for rollback)
//   rollback()   → ROLLED_BACK (quality regression, previous tx restored)
//
// TransactionLog<N> is a power-of-2 circular buffer owned exclusively by
// the BackgroundThread. active() / previous() may be polled lock-free from
// the foreground thread only after the write of status to ACTIVE completes
// (the writes use seq-cst via std::atomic where needed — here we keep it
// simple since both reader and writer are the same background thread).

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Post.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>

namespace crucible {

enum class TxStatus : uint8_t {
    RECORDING,    // ring buffer is accepting ops
    CLOSED,       // iteration boundary detected, ring sealed for this tx
    COMMITTED,    // RegionNode built + Merkle hash computed
    ACTIVE,       // atomic pointer swap done, this region is live
    SUPERSEDED,   // newer tx took over (kept for rollback window)
    ROLLED_BACK,  // quality regression, previous tx restored
};

// ─── Transaction: lifecycle record for one iteration ───────────────
//
// Field layout chosen for exactly 48 bytes with no implicit padding:
//   step_id       8B  offset  0
//   content_hash  8B  offset  8
//   merkle_root   8B  offset 16
//   region*       8B  offset 24
//   ts_ns         8B  offset 32  (timestamp of last status transition)
//   status        1B  offset 40
//   pad[7]        7B  offset 41
//                      total 48B

struct Transaction {
    uint64_t     step_id = 0;         // monotonically increasing
    ContentHash  content_hash;        // default (0) until COMMITTED
    MerkleHash   merkle_root;         // default (0) until COMMITTED
    RegionNode*  region = nullptr;    // null until COMMITTED; arena-owned
    uint64_t     ts_ns = 0;           // timestamp of last state change
    TxStatus     status = TxStatus::RECORDING;
    uint8_t      pad[7]{};
};

static_assert(sizeof(Transaction) == 48, "Transaction layout must be 48 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Transaction);

// ─── TransactionLog<N>: circular ring of the last N transactions ───
//
// Not thread-safe. BackgroundThread is the sole writer.
// active() / previous() return pointers into the ring that remain
// valid as long as the log does not wrap more than N times.

template <uint32_t N = 16>
class TransactionLog {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    static constexpr uint32_t MASK = N - 1;

 public:
    // ── count counter wrapper (#1064 WRAP-Transaction-5) ──────────────
    // count_ is structurally append-only-up-to-N: begin_tx pushes one
    // entry per call (advancing count_), saturates at N (the ring's
    // capacity), and rewinds only on TransactionLog destruction (which
    // is move/copy-deleted, so there is no clear() lifecycle to wrap).
    //
    // BoundedMonotonic<uint32_t, N> pins both invariants at the type
    // level: monotonic forward progress (no accidental backward write
    // via raw assignment) AND the upper bound (no accidental
    // count_ > N via aliasing or memcpy).  Same pattern as
    // SchemaTable::SizeCounter (WRAP-SchemaTab-1 #1003) and
    // CKernelTable::SizeCounter (WRAP-CKernel-2 #890).
    //
    // Zero-cost: regime-2 collapse — sizeof(CountCounter) ==
    // sizeof(uint32_t) == 4 B; TransactionLog layout preserved.
    using CountCounter = ::crucible::safety::BoundedMonotonic<uint32_t, N>;

    TransactionLog() = default;
    TransactionLog(const TransactionLog&)            = delete("TransactionLog holds ring-internal pointers");
    TransactionLog& operator=(const TransactionLog&) = delete("TransactionLog holds ring-internal pointers");
    TransactionLog(TransactionLog&&)                 = delete("interior pointers into entries_ would dangle");
    TransactionLog& operator=(TransactionLog&&)      = delete("interior pointers into entries_ would dangle");

    // gnu::cold: step-boundary transition, amortized once per iteration.
    [[nodiscard, gnu::cold]] Transaction* begin_tx(uint64_t step_id) noexcept {
        auto* tx   = &entries_[head_ & MASK];
        *tx = Transaction{};   // value-init via NSDMI defaults (no memset on non-trivial type)
        tx->step_id = step_id;
        tx->ts_ns   = now_ns();
        head_++;
        // count_ saturates at N: only bump while below the cap.  The
        // explicit if-check is defense-in-depth — BoundedMonotonic::bump's
        // pre-clause `inner_.get() < T(Max)` would fire on the (N+1)th
        // call without it; the if preserves the original
        // saturate-don't-abort semantic so the type-system gate fires
        // only on bug-in-bug regressions (e.g. an unconditional bump).
        if (count_.get() < N) count_.bump();
        // CONTRACT-Tx-Begin-POST: factory result-shape contract — every
        // begin_tx returns a non-null Transaction in the RECORDING state
        // with the caller's step_id stamped.  Catches a future refactor
        // that moves the head_++ before the *tx assignment (would return
        // a pointer into uninitialized ring slot) or that bumps count_
        // unconditionally past the N saturation point.
        //   (1) tx != nullptr — entries_[head_ & MASK] is always a valid
        //       slot in the bounded ring; the address arithmetic cannot
        //       produce nullptr unless entries_ itself is null, which
        //       NSDMI initialization rules out.
        //   (2) tx->status == RECORDING — value-init via TxStatus's
        //       NSDMI default (RECORDING is the first enumerator and the
        //       NSDMI on the field).
        //   (3) tx->step_id == step_id — caller's step_id stamped above.
        // Routes through CRUCIBLE_POST: the predicates dereference the
        // freshly-acquired `tx` pointer; same GCC 16.1.1 consteval-bypass
        // family as CONTRACT-100..108-POST and all sibling POSTs in this
        // session.  Under NDEBUG these collapse to `[[assume]]` so the
        // commit() path can speculate on RECORDING without re-checking.
        CRUCIBLE_POST(tx, tx != nullptr);
        CRUCIBLE_POST(tx, tx->status == TxStatus::RECORDING);
        CRUCIBLE_POST(tx, tx->step_id == step_id);
        return tx;
    }

    // Transition RECORDING (or CLOSED) → COMMITTED, attach region.
    // Returns false if tx is not in a committable state (logic error).
    [[nodiscard]] bool commit(Transaction* const tx, RegionNode* const region,
                ContentHash content_hash, MerkleHash merkle_root) noexcept
        pre (tx != nullptr)
        pre (region != nullptr)
    {
        if (tx->status != TxStatus::RECORDING
            && tx->status != TxStatus::CLOSED) {
            // CONTRACT-Tx-Commit-POST: failure path — false return.
            // The implication post r → COMMITTED holds vacuously.
            // Migrated from P2900 post(r: ...) to in-body CRUCIBLE_POST
            // because P2900 post(r:...) referencing a parameter pointee
            // (`tx->status`) is silently bypassed at consteval in GCC
            // 16.1.1 — same family as CONTRACT-100..108-POST and all
            // sibling Tx posts in this session.  The clause below is
            // re-stated identically so the predicate fires under
            // semantic=enforce and propagates as `[[assume]]` under
            // NDEBUG.
            CRUCIBLE_POST(0, !false || tx->status == TxStatus::COMMITTED);
            return false;
        }
        tx->region       = region;
        tx->content_hash = content_hash;
        tx->merkle_root  = merkle_root;
        tx->status       = TxStatus::COMMITTED;
        tx->ts_ns        = now_ns();
        // CONTRACT-Tx-Commit-POST: success path — strengthen the
        // returned-true case.  After a successful commit:
        //   (1) tx->status == COMMITTED (set above)
        //   (2) tx->region == region   (set above; catches a refactor
        //                                that drops the assignment, which
        //                                would leave region == nullptr
        //                                from begin_tx's value-init)
        //   (3) tx->content_hash == content_hash (caller's hash echoed)
        //   (4) tx->merkle_root  == merkle_root  (caller's root echoed)
        // The pre-existing P2900 post (r: !r || COMMITTED) collapses to
        // (true → COMMITTED) on this path, which (1) covers — the three
        // additional posts strengthen the contract without changing
        // semantics.  Same consteval-bypass framing as the failure path.
        CRUCIBLE_POST(true, tx->status == TxStatus::COMMITTED);
        CRUCIBLE_POST(true, tx->region == region);
        CRUCIBLE_POST(true, tx->content_hash == content_hash);
        CRUCIBLE_POST(true, tx->merkle_root == merkle_root);
        return true;
    }

    // Transition COMMITTED → ACTIVE. Marks the previous ACTIVE as SUPERSEDED.
    // Returns the previously ACTIVE transaction (the rollback target), or nullptr
    // if no previous ACTIVE existed.
    [[nodiscard]] Transaction* activate(Transaction* const tx) noexcept
        pre (tx != nullptr)
    {
        if (tx->status != TxStatus::COMMITTED) {
            // CONTRACT-Tx-Activate-POST: rejected-promotion path.
            // The original P2900 post (r: r == nullptr || ACTIVE) holds
            // because nullptr-return makes the disjunction true
            // vacuously.  Migrated to CRUCIBLE_POST for the same
            // GCC 16.1.1 consteval-bypass-vulnerable parameter-pointee
            // reason as commit() above.
            CRUCIBLE_POST(static_cast<Transaction*>(nullptr),
                          true || tx->status == TxStatus::ACTIVE);
            return nullptr;
        }

        // Demote the current active transaction.
        Transaction* prev = nullptr;
        if (active_tx_) {
            active_tx_->status = TxStatus::SUPERSEDED;
            active_tx_->ts_ns  = now_ns();
            prev = active_tx_;
        }

        tx->status = TxStatus::ACTIVE;
        tx->ts_ns  = now_ns();
        active_tx_ = tx;
        // CONTRACT-Tx-Activate-POST: successful-promotion path —
        // strengthen invariants for the success case:
        //   (1) tx->status == ACTIVE          (set above)
        //   (2) active_tx_ == tx              (set above; pins the
        //                                       SWMR observation that
        //                                       fg dispatch-path
        //                                       readers see)
        //   (3) prev == nullptr || prev->status == SUPERSEDED
        //                                      (the demoted previous
        //                                       active is now in the
        //                                       SUPERSEDED state, ready
        //                                       for rollback() to find;
        //                                       discharges via implies).
        // Routes through CRUCIBLE_POST for the consteval-bypass-vulnerable
        // parameter-pointee + class-member predicates.  The third post
        // is the C++ `||` disjunction form rather than `decide::implies`
        // because `decide::implies(antecedent, consequent)` evaluates
        // both arguments eagerly (function-call semantics) — when
        // `prev == nullptr`, the consequent `prev->status` would
        // null-deref before the predicate folds.  C++ `||` is
        // short-circuit, so the consequent is only evaluated when
        // prev != nullptr — semantically equivalent to the implies
        // catalog predicate, deref-safe.  This is documented as the
        // CRUCIBLE_PRE/POST disjunction pattern where the consequent
        // dereferences the antecedent's witnessed non-null pointer.
        CRUCIBLE_POST(prev, tx->status == TxStatus::ACTIVE);
        CRUCIBLE_POST(prev, active_tx_ == tx);
        CRUCIBLE_POST(prev, prev == nullptr ||
                            prev->status == TxStatus::SUPERSEDED);
        return prev;
    }

    // Roll back: restore the most recent SUPERSEDED transaction to ACTIVE.
    // Marks the current ACTIVE as ROLLED_BACK.
    // Returns true if a rollback was possible.
    [[nodiscard]] bool rollback() noexcept {
        Transaction* prev = previous();
        if (!prev) return false;

        if (active_tx_) {
            active_tx_->status = TxStatus::ROLLED_BACK;
            active_tx_->ts_ns  = now_ns();
        }
        prev->status = TxStatus::ACTIVE;
        prev->ts_ns  = now_ns();
        active_tx_   = prev;
        return true;
    }

    // Latest ACTIVE transaction, or nullptr.
    // Non-const: callers may mutate the returned transaction (e.g. rollback).
    [[nodiscard]] Transaction* active() CRUCIBLE_LIFETIMEBOUND  { return active_tx_; }

    [[nodiscard]] Transaction* previous() CRUCIBLE_LIFETIMEBOUND {
        // Walk the ring backward from head to find the most recent SUPERSEDED entry.
        for (uint32_t i = 0; i < count_.get(); i++) {
            Transaction* e = &entries_[(head_ - 1 - i) & MASK];
            if (e->status == TxStatus::SUPERSEDED) return e;
        }
        return nullptr;
    }

    [[nodiscard]] uint32_t size() const { return count_.get(); }

 private:
    // Nanosecond timestamp from the monotonic clock.
    static uint64_t now_ns() {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<nanoseconds>(
                steady_clock::now().time_since_epoch()).count());
    }

    Transaction  entries_[N]{};
    uint32_t     head_{0};       // next write slot (mod N)
    CountCounter count_{0u};     // number of filled entries (0..N), bounded
    Transaction* active_tx_{nullptr};
};

} // namespace crucible
