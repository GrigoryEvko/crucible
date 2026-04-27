#pragma once

// ═══════════════════════════════════════════════════════════════════
// VyukovMpmcRing<T, Capacity> — Dmitry Vyukov 2011 bounded MPMC
//
// Reference implementation of the original Vyukov bounded MPMC FIFO,
// kept SOLELY for head-to-head benchmark against the SCQ-based
// MpmcRing.  SCQ (Nikolaev DISC 2019) claims ~2× throughput at high
// contention by replacing Vyukov's CAS-on-position with FAA-on-
// position (FAA never fails).  This primitive is the baseline that
// claim is measured against.
//
// ─── Algorithm (Vyukov 2011) ───────────────────────────────────────
//
//   struct Cell {
//       atomic<size_t> sequence;
//       T              data;
//   };
//
//   try_push(item):
//     pos = enqueue_pos.load(relaxed)
//     loop:
//       cell = &buffer[pos & MASK]
//       seq  = cell.sequence.load(acquire)
//       diff = (intptr_t)seq - (intptr_t)pos
//       if diff == 0:                          // ready for produce
//         if enqueue_pos.CAS(pos, pos+1, relaxed):
//           break
//       elif diff < 0:                         // queue full
//         return false
//       else:                                  // CAS race; retry
//         pos = enqueue_pos.load(relaxed)
//     cell.data = item
//     cell.sequence.store(pos + 1, release)    // mark ready for consume
//     return true
//
//   try_pop():
//     pos = dequeue_pos.load(relaxed)
//     loop:
//       cell = &buffer[pos & MASK]
//       seq  = cell.sequence.load(acquire)
//       diff = (intptr_t)seq - (intptr_t)(pos + 1)
//       if diff == 0:                          // ready for consume
//         if dequeue_pos.CAS(pos, pos+1, relaxed):
//           break
//       elif diff < 0:                         // queue empty
//         return nullopt
//       else:
//         pos = dequeue_pos.load(relaxed)
//     item = cell.data
//     cell.sequence.store(pos + Capacity, release)  // ready for next produce
//     return item
//
// ─── Why the 2× gap to SCQ exists ──────────────────────────────────
//
// At low contention (1 producer, 1 consumer):
//   * Vyukov CAS succeeds on first try → ~15-20 ns.
//   * SCQ FAA never fails → ~10-15 ns.
//   * Gap is the per-call CAS-vs-FAA cost; ~30%.
//
// At high contention (16 producers):
//   * Vyukov: all 16 read same enqueue_pos, all CAS the same value,
//     1 wins, 15 retry (and re-read the new pos).  CAS-retry storm
//     turns the head's cache line into a ping-pong source —
//     throughput collapses to ~5× the 1-thread number, not 16×.
//   * SCQ: all 16 FAA enqueue_pos, all get distinct claim values,
//     no retries, no CAS-loss work wasted.  Throughput scales
//     near-linearly with producer count up to memory-controller
//     bandwidth.
//   * Measured gap at 16-way contention: ~2× (per Nikolaev DISC 2019
//     benchmarks; matches our local x86 numbers).
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * T trivially-copyable + trivially-destructible.
//   * Capacity is a power of two, ≥ 2.
//   * Sequence-init: cell[i].sequence = i for i in [0, Capacity).
//     Without this initialization, the "diff == 0" check would fail
//     on the first push.
//
// ─── References ─────────────────────────────────────────────────────
//
//   D. Vyukov, "Bounded MPMC queue", 1024cores.net, 2011.
//     https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
//   THREADING.md §5.5.1 — comparison table SCQ vs Vyukov vs LCRQ
//   concurrent/MpmcRing.h — the production SCQ-based primitive
//   bench/bench_mpmc_scq_vs_vyukov.cpp — the head-to-head harness
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

template <typename T>
concept VyukovValue =
    std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T>;

template <VyukovValue T, std::size_t Capacity>
class VyukovMpmcRing : public safety::Pinned<VyukovMpmcRing<T, Capacity>> {
public:
    static_assert(std::has_single_bit(Capacity),
                  "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

private:
    static constexpr std::size_t MASK = Capacity - 1;

    struct alignas(64) Cell {
        std::atomic<std::size_t> sequence;
        T                        data;
    };

public:
    VyukovMpmcRing() noexcept {
        // Sequence init: cell[i].sequence = i.  This is what makes
        // the first push at pos=0 see "diff = 0 - 0 = 0" → ready.
        for (std::size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    // ── try_push (any producer) ──────────────────────────────────
    //
    // Memory ordering (Vyukov 2011, exact):
    //   - load enqueue_pos: relaxed (any inconsistency is resolved
    //     by the CAS below; relaxed is sufficient because the diff
    //     check + CAS together establish the invariant)
    //   - load cell.sequence: acquire (pair with consumer's
    //     release-store on sequence after consume)
    //   - CAS enqueue_pos: relaxed on both success+failure
    //     (the cell.sequence store(release) below is what
    //     synchronizes with the consumer's load(acquire))
    //   - cell.data store: relaxed (atomicity unnecessary; only
    //     visible after the sequence store(release) below)
    //   - cell.sequence store: release (publishes the data write
    //     to consumers' acquire on sequence)
    [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        Cell* cell = nullptr;
        for (;;) {
            cell = &buffer_[pos & MASK];
            const std::size_t seq =
                cell->sequence.load(std::memory_order_acquire);
            const std::intptr_t diff =
                static_cast<std::intptr_t>(seq) -
                static_cast<std::intptr_t>(pos);
            if (diff == 0) {
                // Ready for produce — try to claim.
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    break;
                }
                // CAS lost; re-read pos (compare_exchange_weak
                // already updated it).
            } else if (diff < 0) {
                return false;  // full
            } else {
                // Another producer claimed pos; reload.
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // ── try_pop (any consumer) ───────────────────────────────────
    //
    // Symmetric to try_push.  The sequence ready-for-consume
    // condition is `seq == pos + 1` (producer set it after writing
    // data).  After consuming, we set `sequence = pos + Capacity`
    // — that's the value the next producer sees as "ready for
    // produce" at the same cell index in the next round.
    [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        Cell* cell = nullptr;
        for (;;) {
            cell = &buffer_[pos & MASK];
            const std::size_t seq =
                cell->sequence.load(std::memory_order_acquire);
            const std::intptr_t diff =
                static_cast<std::intptr_t>(seq) -
                static_cast<std::intptr_t>(pos + 1);
            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return std::nullopt;  // empty
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
        const T result = cell->data;
        cell->sequence.store(pos + Capacity, std::memory_order_release);
        return result;
    }

    // ── Diagnostics (NOT exact) ──────────────────────────────────

    [[nodiscard]] std::size_t size_approx() const noexcept {
        const std::size_t e = enqueue_pos_.load(std::memory_order_relaxed);
        const std::size_t d = dequeue_pos_.load(std::memory_order_relaxed);
        return e > d ? e - d : 0;
    }

    [[nodiscard]] bool empty_approx() const noexcept {
        return size_approx() == 0;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    // enqueue_pos_ and dequeue_pos_ on isolated cache lines — same
    // false-sharing rationale as SpscRing's head/tail.  Cells are
    // also alignas(64) so adjacent cells don't share a line, which
    // matters when concurrent producers/consumers are working on
    // different slots simultaneously.

    alignas(64) std::atomic<std::size_t> enqueue_pos_{0};
    alignas(64) std::atomic<std::size_t> dequeue_pos_{0};
    alignas(64) std::array<Cell, Capacity> buffer_{};
};

}  // namespace crucible::concurrent
