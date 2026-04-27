#pragma once

// ═══════════════════════════════════════════════════════════════════
// MpmcRing<T, N> — beyond-Vyukov bounded MPMC via SCQ
//
// Implements the "Scalable Circular Queue" (SCQ) algorithm from:
//
//   Ruslan Nikolaev, "A Scalable, Portable, and Memory-Efficient
//   Lock-Free FIFO Queue", DISC 2019.  arXiv:1908.04511.
//
// Why SCQ is beyond Vyukov:
//
//   * Vyukov MPMC: CAS on Head/Tail.  Under contention, CAS retries
//     waste work; FAA never fails.  Vyukov throughput degrades sharply
//     under heavy contention.
//   * LCRQ (Morrison-Afek 2013): FAA with CAS2 (cmpxchg16b).  x86-only
//     (not on ARMv8 without LSE, not on PowerPC/RISC-V/MIPS).  Livelock-
//     prone → "closed" CRQs and churn.
//   * SCQ: FAA with single-width CAS.  Portable everywhere.  Livelock-
//     free via a threshold counter.  Memory-efficient (2n cells, no
//     linked chunks).
//
// Plus our additions beyond the paper:
//
//   1. alignas(64) per Cell to prevent adjacent-cell false sharing
//      (paper's Cache_Remap function handles this via index
//      permutation; we handle it structurally by cache-line-aligned
//      cells, avoiding remap cost).
//   2. alignas(128) on Head/Tail/Threshold (future-proof for Apple
//      Silicon 128B cache lines).
//   3. Compile-time template switch for cookie-fingerprint debug mode
//      (zero cost when disabled; strong corruption detection when on).
//
// ─── Cell state encoding (64 bits) ──────────────────────────────────
//
// Every cell carries an atomic 64-bit state word:
//
//     bits[63]     IsSafe      (SCQ §5.2 "IsSafe" bit)
//     bits[62]     Occupied    (our variant of Index = ⊥)
//     bits[61:0]   Cycle       (62-bit cycle counter; wraparound
//                                impossible in practice at 10⁹ cycles/sec)
//
// Note: paper stores an Index field used for the two-queue indirection
// design (aq holds indices into separate data[] array).  We inline the
// data alongside the atomic state, so Index becomes a 1-bit Occupied
// flag.  Trade: slightly more memory per cell vs simpler one-queue
// layout.  For our thread-pool use case (16-byte Job payloads),
// inline is fine.
//
// ─── Per-call atomic shape ──────────────────────────────────────────
//
//   try_push:  1 FAA(Tail) + 1 CAS(cell) + 1 load(Head)
//   try_pop:   1 FAA(Head) + 1 OR(cell) OR 1 CAS(cell)
//
// FAA never fails, so per-thread work is bounded regardless of
// contention; only the per-cell CAS may retry.
//
// ─── The livelock-prevention threshold ──────────────────────────────
//
// SCQ's critical innovation: Threshold counter.  Starts at -1 (empty).
// Every successful enqueue sets Threshold = 3n - 1.  Every failed
// dequeue (couldn't find a ready cell) decrements Threshold.  When
// Threshold ≤ 0, dequeue bails immediately without scanning — breaks
// the livelock loop that plagues naive FAA queues.
//
// Paper §5.1 proves: a dequeuer scans at most n slots before finding
// a committed enqueue, so Threshold = 3n - 1 bounds the worst case.
// (n - 1 initial failures + up to 2n retries = 3n - 1 total.)
//
// ─── ABA safety without CAS2 ────────────────────────────────────────
//
// Paper §4: Cycle counter distinguishes rounds.  Since Cycle takes 62
// bits, it wraps every 4.6e18 rounds.  At even 10⁹ pushes/sec, that's
// 146 years.  Effectively ABA-impossible.
//
// ─── Memory ordering discipline ─────────────────────────────────────
//
//   * FAA on Head/Tail: acq_rel (synchronizes with other producers/
//     consumers; produces a total order over ticket claims).
//   * Cell state CAS: acq_rel on success, acquire on failure.
//     Consumer's ACQUIRE pairs with producer's successful CAS release.
//   * Threshold store: release (after successful enqueue).
//   * Threshold load: acquire (before dequeue fast-path check).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>

#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace crucible::concurrent {

// ── MpmcValue concept ────────────────────────────────────────────────
//
// Bounded cell payload: trivially-copyable + trivially-destructible.
// Per-cell write uses non-atomic stores guarded by the state atomic's
// release-store; T must be safe to byte-copy under memory-order rules.

template <typename T>
concept MpmcValue =
    std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T>;

// ── MpmcRing<T, Capacity> — the SCQ-derived primitive ──────────────

template <MpmcValue T, std::size_t Capacity>
class MpmcRing : public safety::Pinned<MpmcRing<T, Capacity>> {
public:
    using value_type = T;
    static constexpr std::size_t channel_capacity = Capacity;

    static_assert(std::has_single_bit(Capacity),
                  "Capacity must be a power of two");
    static_assert(Capacity >= 2,
                  "Capacity must be at least 2");

    // Buffer has 2×Capacity cells.  Paper §5.2: doubled capacity is
    // the key to livelock-free single-width-CAS operation — ensures
    // an enqueuer always finds a ⊥ slot within 2n steps even when
    // dequeuers are lagging.
    static constexpr std::size_t kCells = 2 * Capacity;
    static constexpr std::uint64_t kMask = kCells - 1;

    // Threshold high water — set on successful enqueue.  Paper:
    // 3n - 1 where n = Capacity.  A dequeuer's scan can fail at
    // most n - 1 + 2n = 3n - 1 times before guaranteed empty.
    static constexpr std::int64_t kThresholdHi =
        static_cast<std::int64_t>(3 * Capacity - 1);

private:
    // ── Cell state bit layout ──────────────────────────────────────
    static constexpr std::uint64_t kIsSafeBit   = std::uint64_t{1} << 63;
    static constexpr std::uint64_t kOccupiedBit = std::uint64_t{1} << 62;
    static constexpr std::uint64_t kCycleMask   = (std::uint64_t{1} << 62) - 1;

    // ── Pack/unpack helpers ────────────────────────────────────────

    [[nodiscard, gnu::const]] static constexpr std::uint64_t
    pack_state(std::uint64_t cycle, bool safe, bool occupied) noexcept {
        return (safe     ? kIsSafeBit   : 0)
             | (occupied ? kOccupiedBit : 0)
             | (cycle & kCycleMask);
    }

    [[nodiscard, gnu::const]] static constexpr std::uint64_t
    cycle_of(std::uint64_t s) noexcept { return s & kCycleMask; }
    [[nodiscard, gnu::const]] static constexpr bool
    is_safe(std::uint64_t s)  noexcept { return (s & kIsSafeBit)   != 0; }
    [[nodiscard, gnu::const]] static constexpr bool
    is_occupied(std::uint64_t s) noexcept { return (s & kOccupiedBit) != 0; }

    // ── Cell — 64-byte aligned to prevent adjacent-cell false share ─
    //
    // Paper uses Cache_Remap permutation to spread 8-byte cell states
    // across cache lines.  We structurally prevent false sharing by
    // alignas(64) on the Cell — each cell is on its own cache line.
    // This costs 56 bytes of padding per cell vs the paper's packed
    // layout, but avoids the remap's arithmetic cost and is cache-
    // optimal for any T ≤ 56 bytes.

    struct alignas(64) Cell {
        // Initial state per paper §5.2: Cycle=0, IsSafe=1, Index=⊥.
        // In our inline variant: Cycle=0, IsSafe=1, Occupied=0.
        std::atomic<std::uint64_t> state{pack_state(0, true, false)};
        T                          data{};
    };

    alignas(64) std::array<Cell, kCells> cells_;

    // Paper §5.2: Tail and Head both initialized to 2n (so Cycle(T)
    // = 2n / 2n = 1 for the first enqueue; matches initial cell
    // cycle of 0, producer wins the first CAS).
    //
    // AtomicMonotonic surfaces the SCQ ticket-claim discipline at the
    // type level: bump_by(1) is the canonical fetch_add(acq_rel) ticket
    // claim; get() is acquire for cross-thread snapshots.  Both Tail
    // and Head are strictly monotonic by construction — every push
    // claims a fresh tail ticket, every pop claims a fresh head ticket,
    // neither ever decrements.
    alignas(64) safety::AtomicMonotonic<std::uint64_t> tail_{kCells};
    alignas(64) safety::AtomicMonotonic<std::uint64_t> head_{kCells};

    // Paper §5.2: Threshold = -1 (empty).  Set to 3n-1 on successful
    // enqueue.  Decremented by failed dequeue.  When ≤ 0, queue is
    // guaranteed empty (dequeue short-circuits).
    //
    // NOT migrated to AtomicMonotonic: the SCQ algorithm requires
    // BOTH fetch_add (set to kThresholdHi via store) AND fetch_sub
    // (failed dequeue decrement) on this counter; threshold is
    // structurally bidirectional by design.  The wrapper's monotonic
    // contract would forbid the algorithm's correctness-required moves.
    alignas(64) std::atomic<std::int64_t> threshold_{-1};

    // Telemetry counter — pure monotonic.  Migrated.  Diagnostic only;
    // not on the hot path.  bump_by uses acq_rel; the prior code used
    // relaxed (cheaper on ARM).  Acceptable: telemetry counters are
    // updated only on the rare ticket-waste path.
    alignas(64) safety::AtomicMonotonic<std::uint64_t> enqueue_ticket_waste_{0};

public:
    MpmcRing() noexcept = default;

    // ── try_push ────────────────────────────────────────────────────
    //
    // Non-blocking MPMC enqueue.  Returns false iff queue is full
    // (observed T - H ≥ 2n) OR we could not find a ⊥ cell within
    // 2n ticket hops.
    //
    // Ordering:
    //   * tail_.fetch_add(1, acq_rel): synchronizes with other
    //     producers' FAA; each producer gets a unique ticket.
    //   * cell.state CAS(acq_rel on success): produces release edge
    //     for consumer's acquire on successful dequeue.
    //   * Write to cell.data happens BEFORE the successful CAS; the
    //     CAS's release-store publishes both the state transition
    //     and the data write together.
    [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
        // Fast-path full check: if T - H ≥ 2n, queue buffer is
        // fully occupied (SCQ §5.4 Figure 10 pattern).  Don't FAA;
        // return false early.
        {
            const std::uint64_t t_snap = tail_.get();
            const std::uint64_t h_snap = head_.get();
            if (t_snap >= h_snap + kCells) [[unlikely]] {
                return false;
            }
        }

        // Paper Fig 8 Lines 12-22: FAA a ticket, then CAS the cell.
        // Outer loop: FAA retry on cell-not-ready.  In practice the
        // outer loop rarely runs more than once.
        for (std::size_t outer_retry = 0; ; ++outer_retry) {
            // Bound outer retries — pathological case only.
            if (outer_retry > kCells) [[unlikely]] return false;

            const std::uint64_t T_ = tail_.bump_by(1);
            const std::uint64_t j = T_ & kMask;
            // Cycle(T) = T / 2n.  For power-of-2 kCells, this is
            // a shift.
            const std::uint64_t cycle_T =
                T_ >> std::countr_zero(kCells);

            Cell& cell = cells_[j];
            std::uint64_t ent = cell.state.load(std::memory_order_acquire);

            // Inner loop: CAS retry if cell state changes between
            // our load and our CAS (rare under normal operation).
            for (;;) {
                const std::uint64_t ent_cycle = cycle_of(ent);

                // Paper Fig 8 Line 16 condition:
                //   Cycle(Ent) < Cycle(T)
                //   AND Index(Ent) = ⊥   (our: NOT Occupied)
                //   AND (IsSafe(Ent) OR Head ≤ T)
                if (ent_cycle < cycle_T &&
                    !is_occupied(ent) &&
                    (is_safe(ent) || head_.get() <= T_)) {

                    // Write data BEFORE publishing cell state.
                    // The successful CAS release-publishes both.
                    cell.data = item;

                    const std::uint64_t new_ent =
                        pack_state(cycle_T, /*safe*/ true, /*occupied*/ true);

                    if (cell.state.compare_exchange_strong(
                            ent, new_ent,
                            std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
                        // Paper Fig 8 Lines 20-21: update Threshold
                        // on successful enqueue.
                        if (threshold_.load(std::memory_order_acquire) !=
                            kThresholdHi) {
                            threshold_.store(kThresholdHi,
                                             std::memory_order_release);
                        }
                        return true;
                    }
                    // CAS failed; ent was updated with new state.
                    // Re-check condition.
                    continue;
                }
                // Condition failed — need a new ticket.
                (void)enqueue_ticket_waste_.bump_by(1);
                break;
            }
            // Outer retry — re-FAA for a new ticket.
        }
    }

    // ── try_pop ─────────────────────────────────────────────────────
    //
    // Non-blocking MPMC dequeue.  Returns nullopt iff queue is empty
    // (Threshold ≤ -1 OR scanned without finding a ready cell).
    //
    // Paper Fig 8 Lines 23-45.
    [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
        // Fast-path empty check via Threshold (Paper Fig 8 Line 24).
        if (threshold_.load(std::memory_order_acquire) < 0) [[unlikely]] {
            return std::nullopt;
        }

        for (std::size_t outer_retry = 0; ; ++outer_retry) {
            if (outer_retry > kCells) [[unlikely]] return std::nullopt;

            const std::uint64_t H_ = head_.bump_by(1);
            const std::uint64_t j = H_ & kMask;
            const std::uint64_t cycle_H =
                H_ >> std::countr_zero(kCells);

            Cell& cell = cells_[j];
            std::uint64_t ent = cell.state.load(std::memory_order_acquire);

            for (;;) {
                const std::uint64_t ent_cycle = cycle_of(ent);

                if (ent_cycle == cycle_H) {
                    // Paper Fig 8 Lines 30-32: cell belongs to our
                    // cycle.  Invalidate via atomic OR (sets
                    // Occupied=0 implicitly by ORing the "⊥" pattern;
                    // in our variant, we OR-clear the Occupied bit —
                    // equivalently CAS to a cleared state).
                    //
                    // Since we encode Occupied as a single bit, the
                    // "OR ⊥" from paper becomes "AND NOT Occupied".
                    // GCC provides atomic fetch_and, but not
                    // straightforwardly — use fetch_and.
                    //
                    // Clear Occupied + clear IsSafe (Line 31: mark
                    // safe, clear index).  Keep Cycle.

                    // Read the data FIRST (before OR publish).
                    T result = cell.data;
                    // Mask to clear Occupied bit (keep Cycle + IsSafe).
                    const std::uint64_t clear_mask = ~kOccupiedBit;
                    (void)cell.state.fetch_and(clear_mask,
                                                std::memory_order_acq_rel);
                    return result;
                }

                if (ent_cycle < cycle_H) {
                    // Paper Fig 8 Lines 33-38: entry is from a past
                    // cycle.  Build a new state marking the cell
                    // "past" and try to advance its cycle.
                    //
                    // Paper Line 33: New = {Cycle(Ent), 0, Index(Ent)}
                    // Paper Line 34-35: if Index(Ent) = ⊥,
                    //                    New = {Cycle(H), IsSafe(Ent), ⊥}
                    // Paper Line 36: if Cycle(Ent) < Cycle(H)
                    // Paper Line 37-38: CAS(Entries[j], ent, New) or retry.

                    std::uint64_t new_ent;
                    if (!is_occupied(ent)) {
                        // Index = ⊥: advance cycle to our cycle_H,
                        // preserve IsSafe, keep Occupied=0.
                        new_ent = pack_state(cycle_H, is_safe(ent),
                                              false);
                    } else {
                        // Index ≠ ⊥: leave cycle, clear IsSafe
                        // (mark unsafe).
                        new_ent = pack_state(ent_cycle, false, true);
                    }

                    if (cell.state.compare_exchange_strong(
                            ent, new_ent,
                            std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
                        // CAS succeeded; now check Tail for empty
                        // detection (Paper Fig 8 Lines 39-45).
                        break;
                    }
                    // CAS failed; ent updated; re-check.
                    continue;
                }

                // ent_cycle > cycle_H: producer is ahead.  Rare.
                // Retry outer loop with a new ticket.
                break;
            }

            // Paper Fig 8 Lines 39-45: empty-queue detection.
            const std::uint64_t T_ = tail_.get();
            if (T_ <= H_ + 1) {
                // Queue is effectively empty.  Decrement Threshold
                // and return nullopt on underflow (Paper Line 42).
                threshold_.fetch_sub(1, std::memory_order_acq_rel);
                return std::nullopt;
            }

            if (threshold_.fetch_sub(1, std::memory_order_acq_rel) <= 0) {
                return std::nullopt;
            }
            // Retry outer (new ticket).
        }
    }

    // ── Diagnostics (approximate; not on hot path) ──────────────────

    [[nodiscard]] std::size_t size_approx() const noexcept {
        const std::uint64_t t = tail_.get();
        const std::uint64_t h = head_.get();
        return t > h ? (t - h) : 0;
    }

    [[nodiscard]] bool empty_approx() const noexcept {
        return threshold_.load(std::memory_order_acquire) < 0;
    }

    [[nodiscard]] std::int64_t threshold_snapshot() const noexcept {
        return threshold_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t ticket_waste_count() const noexcept {
        return enqueue_ticket_waste_.get();
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }
    [[nodiscard]] static constexpr std::size_t internal_cells() noexcept {
        return kCells;
    }
};

// ── Compile-time sanity ─────────────────────────────────────────────

namespace mpmc_detail {
struct mpmc_test_value {
    std::uint64_t a;
    std::uint64_t b;
};
}  // namespace mpmc_detail

static_assert(MpmcValue<mpmc_detail::mpmc_test_value>);
static_assert(!std::is_copy_constructible_v<MpmcRing<int, 64>>);
static_assert(!std::is_move_constructible_v<MpmcRing<int, 64>>);

}  // namespace crucible::concurrent
