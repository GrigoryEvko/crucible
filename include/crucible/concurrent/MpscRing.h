#pragma once

// ═══════════════════════════════════════════════════════════════════
// MpscRing<T, Capacity> — multi-producer single-consumer ring buffer
//
// Dmitry Vyukov's bounded MPSC array queue (vyukov.com 2007).  Many
// producers push concurrently via CAS on the global head; a single
// consumer pops via simple load/store on the tail.  Per-cell sequence
// numbers self-synchronize each cell's producer-consumer-producer
// hand-off — no global "empty/full" gate to maintain.
//
// ─── Why Vyukov's per-cell sequence is elegant ──────────────────────
//
// Each cell `i` carries a `sequence` counter that doubles as both
// "ready for round-K producer" and "ready for round-K consumer":
//
//   Initial state of cell i:
//     sequence = i           // ready for round-0 producer at pos i
//
//   Producer at pos = i (round 0):
//     Sees seq == pos == i → claims (CAS head_), writes data,
//     stores sequence = pos + 1 (release).
//     Cell now reads "filled, awaiting consumer at pos i".
//
//   Consumer at pos = i:
//     Sees seq == pos + 1 → reads data, stores
//     sequence = pos + Capacity (release).
//     Cell now reads "drained, awaiting round-1 producer at pos = i + Capacity".
//
//   Producer at pos = i + Capacity (round 1):
//     Sees seq == pos = i + Capacity → claims, writes, etc.
//
// Two channels of synchronization through the same per-cell counter:
//   * Producer release on `seq = pos + 1` syncs-with consumer
//     acquire — consumer sees the data write that preceded.
//   * Consumer release on `seq = pos + Capacity` syncs-with
//     next-round producer acquire — producer sees the consumer
//     finished reading the prior data.
//
// No global head/tail-distance check is needed; each cell's sequence
// alone tells producer "may I write?" and consumer "may I read?".
//
// ─── When to use MpscRing vs other primitives ───────────────────────
//
//   MpscRing<T, N>: many producers pushing into ONE shared FIFO
//                   that ONE consumer drains.  Producer pays a CAS;
//                   consumer pays no CAS.  Right for fan-in patterns
//                   where producers are dynamic (number changes) or
//                   global FIFO across producers matters.
//
//   ChaseLevDeque (QUEUE-2): owner is BOTH producer and consumer
//                   (LIFO at bottom); thieves only steal from top.
//                   For fork-join thread pools.
//
//   AtomicSnapshot (QUEUE-1): one writer publishes the LATEST T;
//                   readers see the most recent value, NOT each
//                   intermediate one.  For metrics broadcast.
//
//   SPSC TraceRing / MetaLog: ONE producer, ONE consumer; cheaper
//                   than MpscRing because no CAS needed at all.
//                   Use for known SPSC channels.
//
// Specifically, do NOT use MpscRing for: TraceRing, MetaLog (SPSC
// is correct and faster), kernel compile dispatch (use ChaseLevDeque
// for work-stealing).  MPSC's CAS cost is justified ONLY when
// producers are genuinely dynamic.
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * T trivially-copyable + trivially-destructible (per-cell
//     direct assignment, no constructor races).
//   * Capacity must be a power of two and > 0.
//   * sizeof(T) is padded into a 64-byte cache-line cell with the
//     sequence counter; T larger than ~56 bytes spans multiple
//     cache lines per cell — still correct but losing some cache
//     density.  For very large payloads, store T* and allocate
//     bodies elsewhere.
//
// ─── Per-call atomic shape ──────────────────────────────────────────
//
//   try_push (uncontended):       1 CAS + 1 release
//   try_push (under contention):  CAS may retry until the producer's
//                                 ticket lands on a free slot
//   try_pop  (uncontended):       no CAS, single consumer issues
//                                 a load + release
//   Capacity = power-of-2; choose to absorb peak burst from all
//   producers' worst-case backlog.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
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

// ── RingValue<T> concept ──────────────────────────────────────────
//
// Constraints on T for safe storage in a non-atomic cell field
// guarded by a per-cell sequence atomic.

template <typename T>
concept RingValue =
    std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T>;

// ── MpscRing<T, Capacity> ─────────────────────────────────────────

template <RingValue T, std::size_t Capacity>
class MpscRing : public safety::Pinned<MpscRing<T, Capacity>> {
public:
    static_assert(std::has_single_bit(Capacity),
                  "Capacity must be a power of two");
    static_assert(Capacity > 0,
                  "Capacity must be greater than zero");

private:
    static constexpr std::uint64_t MASK = std::uint64_t{Capacity - 1};

    // Each cell on its own cache line — without alignas(64), two
    // producers writing to adjacent cells would false-share the
    // same line, serializing what should be concurrent traffic.
    struct alignas(64) Cell {
        std::atomic<std::uint64_t> sequence{0};
        T                          data{};
    };

public:
    MpscRing() noexcept {
        // Per-cell initial sequence = cell index — this seeds
        // each cell as "ready for round-0 producer at pos i".
        for (std::uint64_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    // ── try_push (any producer) ───────────────────────────────────
    //
    // Multi-producer enqueue.  CAS on head_ to claim a position;
    // on success, write data and release the cell sequence to the
    // consumer.
    //
    // Returns false on full queue.  Caller may retry / yield;
    // primitive does not spin.
    //
    // Memory ordering:
    //   - head_.load(relaxed): own first read (we'll CAS-validate)
    //   - cell.sequence.load(acquire): syncs-with prior consumer's
    //     release on cell.sequence (so we know cell is FREE for
    //     this round, AND if filled-by-prior-round, we'd have seen
    //     a different sequence value).
    //   - head_.compare_exchange_weak(relaxed): pos claim.  CAS
    //     failure is benign — we re-read head and retry.  Relaxed
    //     suffices because the cell.sequence load above already
    //     established the synchronization edge with the consumer.
    //   - cell.data = item: non-atomic, but no race because no
    //     other producer or consumer can touch this cell until
    //     we release the sequence below.
    //   - cell.sequence.store(pos+1, release): publishes data
    //     write to consumer's acquire.
    [[nodiscard]] bool try_push(T item) noexcept {
        Cell* cell = nullptr;
        std::uint64_t pos = head_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & MASK];
            const std::uint64_t seq =
                cell->sequence.load(std::memory_order_acquire);
            const std::int64_t diff =
                static_cast<std::int64_t>(seq) -
                static_cast<std::int64_t>(pos);

            if (diff == 0) {
                // Cell is ready for THIS producer (pos).  Try to
                // claim by advancing head.  CAS-weak is fine —
                // spurious failure just retries.
                if (head_.compare_exchange_weak(pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    break;
                }
                // CAS failed — pos is updated by CAS to current
                // head value.  Loop continues with refreshed pos.
            } else if (diff < 0) {
                // Cell still holds an older round's sequence —
                // queue is full (cell hasn't been drained for
                // its prior fill).
                return false;
            } else {
                // diff > 0 — another producer raced ahead and
                // claimed a higher pos; refresh and retry.
                pos = head_.load(std::memory_order_relaxed);
            }
        }

        // We own slot at pos.  Write data and release the cell.
        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // ── try_pop (single consumer ONLY) ────────────────────────────
    //
    // Single-consumer dequeue.  No CAS — only one consumer can
    // touch tail_, so a plain load/store suffices.  Caller MUST
    // guarantee no concurrent try_pop calls.
    //
    // Returns nullopt if queue is empty.  Single consumer never
    // sees "in-progress producer" state distinctly from "empty"
    // (both look like seq != pos+1); the seq < pos+1 case maps
    // to "not yet published" which the consumer treats as empty.
    //
    // Memory ordering:
    //   - tail_.load(relaxed): own variable, no cross-thread sync
    //   - cell.sequence.load(acquire): syncs-with producer's
    //     release on cell.sequence — if seq == pos+1, all of
    //     the producer's data writes are visible.
    //   - cell.data read: non-atomic but happens-after the
    //     acquire load above
    //   - cell.sequence.store(pos+Capacity, release): publishes
    //     "drained" to next-round producer's acquire (in try_push
    //     above).
    //   - tail_.store(pos+1, relaxed): own variable; no cross-
    //     thread sync needed (producers don't read tail).
    [[nodiscard]] std::optional<T> try_pop() noexcept {
        const std::uint64_t pos = tail_.load(std::memory_order_relaxed);
        Cell* cell = &buffer_[pos & MASK];
        const std::uint64_t seq =
            cell->sequence.load(std::memory_order_acquire);
        const std::int64_t diff =
            static_cast<std::int64_t>(seq) -
            static_cast<std::int64_t>(pos + 1);

        if (diff != 0) {
            // diff < 0: producer hasn't published yet (empty)
            // diff > 0: shouldn't happen with single consumer +
            //           sequential consumer pos advancement.
            //           Treat as empty (the consumer state will
            //           heal when the producer catches up).
            return std::nullopt;
        }

        const T item = cell->data;
        cell->sequence.store(pos + Capacity, std::memory_order_release);
        tail_.store(pos + 1, std::memory_order_relaxed);
        return item;
    }

    // ── empty_approx (any thread, NOT exact) ──────────────────────
    //
    // Snapshot read — value may change immediately after return.
    // Useful for "should we keep polling?" decisions, NEVER for
    // correctness invariants.

    [[nodiscard]] bool empty_approx() const noexcept {
        const std::uint64_t pos =
            tail_.load(std::memory_order_acquire);
        const std::uint64_t seq =
            buffer_[pos & MASK].sequence.load(std::memory_order_acquire);
        // If seq < pos + 1 → producer hasn't published at this pos
        // yet → empty (or in flight).
        return static_cast<std::int64_t>(seq) -
               static_cast<std::int64_t>(pos + 1) < 0;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    // ── Storage layout ────────────────────────────────────────────
    //
    // head_ on its own cache line: contended by all producers
    // (CAS retry traffic).  Isolating prevents tail_'s line from
    // being invalidated by producer activity.
    //
    // tail_ on its own cache line: written only by the single
    // consumer; producers never read tail (the per-cell sequence
    // protocol makes tail invisible to them).  Isolating prevents
    // consumer's stores from invalidating head_'s line.
    //
    // buffer_ aligned: each Cell is alignas(64) so cells don't
    // false-share with each other.

    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
    alignas(64) std::array<Cell, Capacity> buffer_{};
};

}  // namespace crucible::concurrent
