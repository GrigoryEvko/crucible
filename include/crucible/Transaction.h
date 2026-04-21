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
    TransactionLog() = default;
    TransactionLog(const TransactionLog&)            = delete("TransactionLog holds ring-internal pointers");
    TransactionLog& operator=(const TransactionLog&) = delete("TransactionLog holds ring-internal pointers");
    TransactionLog(TransactionLog&&)                 = delete("interior pointers into entries_ would dangle");
    TransactionLog& operator=(TransactionLog&&)      = delete("interior pointers into entries_ would dangle");

    // gnu::cold: step-boundary transition, amortized once per iteration.
    [[gnu::cold]] Transaction* begin_tx(uint64_t step_id) {
        auto* tx   = &entries_[head_ & MASK];
        *tx = Transaction{};   // value-init via NSDMI defaults (no memset on non-trivial type)
        tx->step_id = step_id;
        tx->ts_ns   = now_ns();
        head_++;
        if (count_ < N) count_++;
        return tx;
    }

    // Transition RECORDING (or CLOSED) → COMMITTED, attach region.
    // Returns false if tx is not in a committable state (logic error).
    bool commit(Transaction* tx, RegionNode* region,
                ContentHash content_hash, MerkleHash merkle_root)
        pre (tx != nullptr)
        pre (region != nullptr)
    {
        if (tx->status != TxStatus::RECORDING
            && tx->status != TxStatus::CLOSED) {
            return false;
        }
        tx->region       = region;
        tx->content_hash = content_hash;
        tx->merkle_root  = merkle_root;
        tx->status       = TxStatus::COMMITTED;
        tx->ts_ns        = now_ns();
        return true;
    }

    // Transition COMMITTED → ACTIVE. Marks the previous ACTIVE as SUPERSEDED.
    // Returns the previously ACTIVE transaction (the rollback target), or nullptr
    // if no previous ACTIVE existed.
    [[nodiscard]] Transaction* activate(Transaction* tx)
        pre (tx != nullptr)
    {
        if (tx->status != TxStatus::COMMITTED) return nullptr;

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
        return prev;
    }

    // Roll back: restore the most recent SUPERSEDED transaction to ACTIVE.
    // Marks the current ACTIVE as ROLLED_BACK.
    // Returns true if a rollback was possible.
    bool rollback() {
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
        for (uint32_t i = 0; i < count_; i++) {
            Transaction* e = &entries_[(head_ - 1 - i) & MASK];
            if (e->status == TxStatus::SUPERSEDED) return e;
        }
        return nullptr;
    }

    [[nodiscard]] uint32_t size() const { return count_; }

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
    uint32_t     count_{0};      // number of filled entries (0..N)
    Transaction* active_tx_{nullptr};
};

} // namespace crucible
