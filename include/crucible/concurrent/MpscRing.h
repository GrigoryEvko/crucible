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
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>

#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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
    //
    // sequence wrapped as AtomicMonotonic — every round advances the
    // sequence forward by either +1 (producer claim → publish) or
    // +Capacity-1 (consumer drain → next-round producer ready).  The
    // monotonic contract on store catches accidental backward writes
    // at compile-time; hot-path codegen identical to bare atomic.
    struct alignas(64) Cell {
        safety::AtomicMonotonic<std::uint64_t> sequence{0};
        T                                      data{};
    };

public:
    MpscRing() noexcept {
        // Per-cell initial sequence = cell index — this seeds
        // each cell as "ready for round-0 producer at pos i".
        // reset_under_quiescence bypasses the monotonic-store
        // contract; initial seeding is the canonical use case
        // for that API (no other thread holds a reference yet).
        for (std::uint64_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.reset_under_quiescence(i);
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
        std::uint64_t pos = head_.peek_relaxed();
        for (;;) {
            cell = &buffer_[pos & MASK];
            const std::uint64_t seq = cell->sequence.get();  // acquire
            const std::int64_t diff =
                static_cast<std::int64_t>(seq) -
                static_cast<std::int64_t>(pos);

            if (diff == 0) {
                // Cell is ready for THIS producer (pos).  Try to
                // claim by advancing head.  CAS-weak is fine —
                // spurious failure just retries.  Monotonic CAS
                // carries the (pos < pos+1) pre — backward CAS is
                // a compile-time impossibility on this surface.
                if (head_.compare_exchange_advance_weak(pos, pos + 1,
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
                pos = head_.peek_relaxed();
            }
        }

        // We own slot at pos.  Write data and release the cell.
        // Monotonic store: pos+1 > pos == prior cell sequence value
        // (that's how we won the CAS above), so the contract holds.
        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // ── try_push_batch (any producer) ─────────────────────────────
    //
    // Claim N consecutive cells in ONE CAS, then publish data + cell
    // sequences via pure stores.  Per-batch atomic cost: 1 CAS + 1
    // sequence-load.  Per-item cost amortizes toward the L1d/L2 store-
    // buffer floor.
    //
    // Algorithm:
    //
    //   1. Read head_ → pos (relaxed; will validate via CAS).
    //
    //   2. Read cell[(pos + N - 1) & MASK].sequence (acquire) — the
    //      LAST cell in the proposed batch.  Single consumer drains
    //      cells in monotonic order, so if the LAST cell is ready
    //      (sequence == pos + N - 1), every earlier cell is also
    //      ready (consumer reached at least pos + N - capacity).
    //      One acquire-load suffices to validate the whole batch.
    //
    //   3. CAS head_ from pos to pos + N — claims all N cells in one
    //      atomic.  Any other producer's load of head_ now sees pos+N,
    //      they cannot claim cells in [pos, pos+N).  The consumer has
    //      not yet seen any cell.sequence advance; cells remain
    //      "not yet ready" from consumer's perspective.
    //
    //   4. For i in 0..N-1: write cell[(pos + i) & MASK].data = items[i].
    //      Pure non-atomic stores — this producer exclusively owns these
    //      cells until step 5 publishes them.  No false sharing with
    //      other producers (no one else can write to these cells); no
    //      contention with consumer (consumer can't read until cell.
    //      sequence advances).  Pure memcpy through L1d store buffer.
    //
    //   5. For i in 0..N-1: cell[(pos+i) & MASK].sequence.store(
    //          pos + i + 1, release) — publishes cell to consumer.
    //      On x86, release-store is a plain MOV (TSO gives release
    //      semantics free); on ARM, STLR (single instruction).  No
    //      atomic-RMW per cell — just N stores.
    //
    // Returns the number of items pushed (N on success, 0 on full).
    // Either ALL items push or NONE — no partial batches.  Caller's
    // span is unaffected on failure (no partial fill).
    //
    // Cost model:
    //   uncontended:  1 CAS + 1 acq-load + N data stores + N seq
    //                 release-stores = ~5 cycles + 2N stores
    //   contended:    CAS retries amortized across N items
    //
    // Constraints:
    //   * N must satisfy N ≤ Capacity (else batch could never fit)
    //   * items.size() ≤ Capacity is checked at runtime; oversized
    //     spans return 0 immediately.

    [[nodiscard, gnu::hot]] std::size_t try_push_batch(
        std::span<const T> items) noexcept {
        const std::size_t N = items.size();
        if (N == 0) return 0;
        if (N > Capacity) [[unlikely]] return 0;

        for (;;) {
            const std::uint64_t pos = head_.peek_relaxed();

            // Check the LAST cell in the proposed batch — sequential
            // consumer drain means LAST-ready ⇒ all earlier cells ready.
            const std::uint64_t last_idx = pos + N - 1;
            Cell& last_cell = buffer_[last_idx & MASK];
            const std::uint64_t last_seq = last_cell.sequence.get();  // acquire
            const std::int64_t diff =
                static_cast<std::int64_t>(last_seq) -
                static_cast<std::int64_t>(last_idx);

            if (diff < 0) {
                // Cell sequence still at older round — consumer hasn't
                // drained enough.  Queue is "full" relative to N.
                return 0;
            }
            if (diff > 0) {
                // Another producer has already advanced past our pos.
                // Reload head and retry.
                continue;
            }

            // Atomic claim: advance head by N.  Monotonic CAS: pre
            // (pos < pos + N) holds since N > 0.  Weak CAS is fine —
            // spurious failure just retries.
            std::uint64_t expected = pos;
            if (!head_.compare_exchange_advance_weak(expected, pos + N,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                continue;
            }

            // We own cells [pos, pos+N).  Publish via pure stores.
            for (std::size_t i = 0; i < N; ++i) {
                Cell& c = buffer_[(pos + i) & MASK];
                c.data = items[i];
                // Release-store on cell.sequence publishes the data
                // write above to the consumer's acquire-load in
                // try_pop / try_pop_batch.  Forward-monotonic
                // (sequence advances by 1 per round).
                c.sequence.store(pos + i + 1, std::memory_order_release);
            }
            return N;
        }
    }

    // ── try_pop_batch (single consumer ONLY) ──────────────────────
    //
    // Drain up to N cells in one call.  Single consumer means tail_ is
    // exclusively owned — no atomic ticket claim needed.  Per-item
    // cost: 1 acquire-load on cell.sequence + 1 data read + 1 release-
    // store on cell.sequence.  No CAS, no FAA.
    //
    // Returns the number of items popped (≤ items.size(); 0 iff queue
    // empty at the moment of the first cell check).  Items written into
    // out[0..return-1] in FIFO order.
    //
    // Caller MUST guarantee single-consumer (no concurrent try_pop or
    // try_pop_batch).

    [[nodiscard, gnu::hot]] std::size_t try_pop_batch(
        std::span<T> out) noexcept {
        const std::size_t cap = out.size();
        if (cap == 0) return 0;

        const std::uint64_t pos0 = tail_.peek_relaxed();
        std::size_t n = 0;

        for (; n < cap; ++n) {
            const std::uint64_t pos = pos0 + n;
            Cell& cell = buffer_[pos & MASK];
            const std::uint64_t seq = cell.sequence.get();  // acquire
            const std::int64_t diff =
                static_cast<std::int64_t>(seq) -
                static_cast<std::int64_t>(pos + 1);

            if (diff != 0) {
                // Producer hasn't published cell at pos yet (diff < 0)
                // or in an inconsistent future state (diff > 0, won't
                // happen with sequential consumer).  Stop the batch.
                break;
            }

            out[n] = cell.data;
            // Release: publishes "drained" to next-round producer.
            cell.sequence.store(pos + Capacity, std::memory_order_release);
        }

        if (n > 0) {
            // Single tail update covering all drained cells.
            tail_.store(pos0 + n, std::memory_order_relaxed);
        }
        return n;
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
        const std::uint64_t pos = tail_.peek_relaxed();
        Cell* cell = &buffer_[pos & MASK];
        const std::uint64_t seq = cell->sequence.get();  // acquire
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
        // Both stores forward-monotonic:
        //   cell.sequence: pos+1 → pos+Capacity (increment Capacity-1)
        //   tail_:         pos   → pos+1
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
        const std::uint64_t pos = tail_.get();                     // acquire
        const std::uint64_t seq = buffer_[pos & MASK].sequence.get(); // acquire
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

    // head_ / tail_ migrated to AtomicMonotonic per CONCURRENT-A4.
    // Both counters are forward-monotonic by construction: head_ is
    // claimed via CAS (always pos → pos+1), tail_ is advanced by the
    // single consumer (always pos → pos+1).  The wrapper carries the
    // monotonic-direction contract at the type level — backward CAS
    // or backward store is structurally impossible on this surface.
    alignas(64) safety::AtomicMonotonic<std::uint64_t> head_{0};
    alignas(64) safety::AtomicMonotonic<std::uint64_t> tail_{0};
    alignas(64) std::array<Cell, Capacity> buffer_{};
};

}  // namespace crucible::concurrent
