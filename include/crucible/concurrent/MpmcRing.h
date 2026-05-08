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
//     bits[63]     IsSafe         (SCQ §5.2 "IsSafe" bit)
//     bits[62]     Occupied       (our variant of Index = ⊥)
//     bits[61]     DataPublished  (cell.data fully written by producer)
//     bits[60:0]   Cycle          (61-bit cycle counter; wraparound
//                                   impossible in practice — at 10⁹
//                                   cycles/sec, wraps every 73 years)
//
// Note: paper stores an Index field used for the two-queue indirection
// design (aq holds indices into separate data[] array).  We inline the
// data alongside the atomic state, so Index becomes a 1-bit Occupied
// flag.  Trade: slightly more memory per cell vs simpler one-queue
// layout.  For our thread-pool use case (16-byte Job payloads),
// inline is fine.
//
// Why DataPublished is needed (vs paper's bare two-bit state).  In
// paper's indirection design, eidx points into a separate data[]
// array; the producer owns eidx exclusively (via free-index pool) so
// the data write happens before the eidx ever appears in any cell
// — no race possible.  In our INLINE variant, two producers at
// DIFFERENT cycles (T_a at cycle C, T_b = T_a + 2n at cycle C+1) can
// load the same pre-CAS ent and BOTH execute `cell.data = item;`
// before either CAS succeeds; the writes race at the byte level.
// The CAS-loser may have written cell.data LAST in real time, so the
// CAS-winner publishes a state pointing at the CAS-loser's data.
// Effect: a successfully-pushed message gets popped TWICE (once at
// the bogus cycle, once at the producer's retry cycle) while another
// producer's message is silently lost.  Caught by Crucible's
// cookie-fingerprint fuzzer (test_concurrency_collision_fuzzer §8) +
// TSan data race at MpmcRing.h:269 (writer/writer on cell.data).
// Fix: enqueue is now THREE atomic steps — (1) CAS-reserve the cell
// with DataPublished=0, (2) write data (now sole writer), (3) fetch_or
// the DataPublished bit with release.  Consumer's branch-1 spin-waits
// for DataPublished before reading data.  Cost: +1 atomic OR per push,
// +1 conditional branch per pop, +1 spin in the rare reserved-but-
// unpublished window (microseconds in worst case).
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
    static constexpr std::uint64_t kIsSafeBit        = std::uint64_t{1} << 63;
    static constexpr std::uint64_t kOccupiedBit      = std::uint64_t{1} << 62;
    static constexpr std::uint64_t kDataPublishedBit = std::uint64_t{1} << 61;
    static constexpr std::uint64_t kCycleMask        = (std::uint64_t{1} << 61) - 1;

    // ── Pack/unpack helpers ────────────────────────────────────────

    [[nodiscard, gnu::const]] static constexpr std::uint64_t
    pack_state(std::uint64_t cycle, bool safe, bool occupied,
               bool published = false) noexcept {
        return (safe      ? kIsSafeBit        : 0)
             | (occupied  ? kOccupiedBit      : 0)
             | (published ? kDataPublishedBit : 0)
             | (cycle & kCycleMask);
    }

    [[nodiscard, gnu::const]] static constexpr std::uint64_t
    cycle_of(std::uint64_t s) noexcept { return s & kCycleMask; }
    [[nodiscard, gnu::const]] static constexpr bool
    is_safe(std::uint64_t s)  noexcept { return (s & kIsSafeBit)   != 0; }
    [[nodiscard, gnu::const]] static constexpr bool
    is_occupied(std::uint64_t s) noexcept { return (s & kOccupiedBit) != 0; }
    [[nodiscard, gnu::const]] static constexpr bool
    is_data_published(std::uint64_t s) noexcept {
        return (s & kDataPublishedBit) != 0;
    }

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
    //   * cell.state CAS(acq_rel on success): reserves the cell with
    //     DataPublished=0; produces an acquire-release edge but does
    //     NOT publish the data yet.
    //   * cell.data = item: non-atomic write; safe because the CAS
    //     above made this producer the sole owner of the cell for
    //     this cycle.
    //   * cell.state.fetch_or(kDataPublishedBit, release): release-
    //     edge that publishes the data write to consumers.  Paired
    //     with the consumer's acquire-load of cell.state.
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

                    // Two-phase publish.  Phase 1: CAS-reserve the
                    // cell with DataPublished=0.  Only ONE producer
                    // can win this CAS for any given (cycle_T, j) —
                    // the loser's CAS fails and they re-FAA.  After
                    // the CAS, this producer is the SOLE writer of
                    // cell.data for this cycle.  Inline-data SCQ
                    // requires this; without it, two producers at
                    // different cycles targeting the same cell can
                    // race on cell.data (caught by TSan + cookie
                    // fuzzer; see header doc-block above).
                    const std::uint64_t reserved_ent =
                        pack_state(cycle_T,
                                   /*safe*/ true,
                                   /*occupied*/ true,
                                   /*published*/ false);

                    if (!cell.state.compare_exchange_strong(
                            ent, reserved_ent,
                            std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
                        // CAS failed; ent updated.  Re-check.
                        continue;
                    }

                    // Phase 2: we own the cell.  Write data with
                    // non-atomic stores; no other producer can
                    // observe this cell as available now.
                    cell.data = item;

                    // Phase 3: publish.  fetch_or sets DataPublished
                    // atomically without disturbing IsSafe/Occupied/
                    // Cycle bits — important because a consumer at
                    // higher cycle may have CAS'd the IsSafe bit off
                    // (branch-2 occupied path) between our reserve
                    // and our publish.
                    (void)cell.state.fetch_or(kDataPublishedBit,
                                              std::memory_order_release);

                    // Paper Fig 8 Lines 20-21: update Threshold
                    // on successful enqueue.
                    if (threshold_.load(std::memory_order_acquire) !=
                        kThresholdHi) {
                        threshold_.store(kThresholdHi,
                                         std::memory_order_release);
                    }
                    return true;
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
                    // cycle.  Two-phase publish (see try_push doc-
                    // block) means the producer may have CAS-reserved
                    // the cell but not yet OR'd the DataPublished
                    // bit.  Spin-wait for DataPublished before reading
                    // cell.data — without this, the consumer reads
                    // garbage from the gap between phase 1 and phase
                    // 3 of the producer's enqueue.  Worst-case wait
                    // is microseconds (the producer is mid-write,
                    // possibly descheduled).  Ticket uniqueness
                    // guarantees only one consumer claims this head
                    // ticket so the spin is on a single producer's
                    // publish, not arbitrarily-many writers.
                    while (!is_data_published(ent)) [[unlikely]] {
                        CRUCIBLE_SPIN_PAUSE;
                        ent = cell.state.load(std::memory_order_acquire);
                    }

                    // Read the data FIRST (before OR publish).
                    T result = cell.data;
                    // Clear Occupied + DataPublished bits (keep Cycle
                    // + IsSafe).  The DataPublished bit must be
                    // cleared so the next producer's two-phase
                    // protocol starts from a clean slate at this
                    // cell's next cycle.
                    const std::uint64_t clear_mask =
                        ~(kOccupiedBit | kDataPublishedBit);
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
                        // preserve IsSafe, keep Occupied=0.  Reset
                        // DataPublished to 0 — the cell now
                        // represents a fresh-cycle empty slot, no
                        // producer at cycle_H has reserved it yet.
                        new_ent = pack_state(cycle_H, is_safe(ent),
                                              false, /*published*/ false);
                    } else {
                        // Index ≠ ⊥: leave cycle, clear IsSafe
                        // (mark unsafe).  Preserve DataPublished —
                        // the producer at ent_cycle may have already
                        // published, and the matching consumer at
                        // ticket H_match = ent_cycle*2n+j (which is
                        // strictly less than our current H_) will
                        // need to read the data once it claims its
                        // ticket.
                        new_ent = pack_state(ent_cycle, false, true,
                                              is_data_published(ent));
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

    // ── try_push_batch ──────────────────────────────────────────────
    //
    // Batched MPMC push.  Calls try_push per item; returns the number
    // pushed before the first failure.  Per-item amortized cost is
    // higher than SPSC's batched API because each item still pays the
    // SCQ FAA + cell CAS — the batch shape is ergonomic, not a
    // throughput optimization.  Use bare try_push when caller wants
    // per-item retry control.
    //
    // Per-call shape: N × try_push, short-circuit on first false.
    [[nodiscard, gnu::hot]] std::size_t try_push_batch(
        std::span<const T> items) noexcept
    {
        std::size_t pushed = 0;
        for (const T& item : items) {
            if (!try_push(item)) break;
            ++pushed;
        }
        return pushed;
    }

    // ── try_pop_batch ───────────────────────────────────────────────
    //
    // Batched MPMC pop.  Calls try_pop per slot in `out`; returns the
    // number filled before the first nullopt.  Same per-item cost
    // tradeoff as try_push_batch.
    [[nodiscard, gnu::hot]] std::size_t try_pop_batch(std::span<T> out) noexcept {
        std::size_t popped = 0;
        for (T& slot : out) {
            auto v = try_pop();
            if (!v) break;
            slot = std::move(*v);
            ++popped;
        }
        return popped;
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
