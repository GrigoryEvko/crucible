#pragma once

// Bounded lock-free MPMC queue on the SCQ design.  Every index move is
// a fetch_add, which never fails, so a thread's work is bounded
// regardless of contention.  Only the per-cell CAS retries.  Two
// rejected alternatives:
//
//   * CAS on both head and tail (the Vyukov design).  A failed CAS
//     wastes the work that produced it, where a fetch_add cannot fail.
//   * LCRQ, fetch_add plus double-width CAS.  Double-width CAS is not
//     available outside x86, and the design admits livelock.
//
// Cell state is one atomic 64-bit word:
//
//     bit 63     IsSafe
//     bit 62     Occupied
//     bit 61     DataPublished
//     bits 60:0  Cycle
//
// The published algorithm keeps the payload in a separate array and
// stores only an index in the cell.  A producer owns its payload slot
// before that index appears in any cell, so no payload race exists
// there.  This ring inlines the payload beside the state word, which
// reintroduces the race: two producers at different cycles that map to
// the same cell can both load the same pre-CAS state and both write
// cell.data before either CAS resolves.  The CAS loser may write last
// in real time, so the CAS winner publishes a state that points at the
// loser's payload.  One message is then popped twice and a second
// message is lost.
//
// Enqueue is therefore three steps.  CAS-reserve the cell with
// DataPublished clear, write the payload as sole owner, then set
// DataPublished with a release fetch_or.  A consumer waits for
// DataPublished before it reads the payload.
//
// Threshold prevents livelock.  It starts at -1, a successful enqueue
// raises it to 3 × Capacity - 1, and a dequeue that finds no ready cell
// decrements it.  At or below zero a dequeue bails without scanning.
//
// Cycle is wide enough that wraparound is unreachable, so single-width
// CAS is sufficient to defeat ABA.

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
#include <thread>
#include <type_traits>

namespace crucible::concurrent {

template <typename T>
concept MpmcValue = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;

template <MpmcValue T, std::size_t Capacity>
class MpmcRing : public safety::Pinned<MpmcRing<T, Capacity>> {
public:
    using value_type = T;
    static constexpr std::size_t channel_capacity = Capacity;

    static_assert(std::has_single_bit(Capacity), "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

    // Doubling the cell count is what makes single-width CAS
    // livelock-free: an enqueuer always finds an unoccupied cell within
    // 2 × Capacity ticket hops, even while dequeuers lag.
    static constexpr std::size_t kCells = 2 * Capacity;
    static constexpr std::uint64_t kMask = kCells - 1;

    // A dequeuer's scan fails at most Capacity - 1 times before the
    // first committed enqueue, then at most 2 × Capacity more times, so
    // 3 × Capacity - 1 bounds the worst case.
    static constexpr std::int64_t kThresholdHi = static_cast<std::int64_t>(3 * Capacity - 1);

private:
    static constexpr std::uint64_t kIsSafeBit = std::uint64_t{1} << 63;
    static constexpr std::uint64_t kOccupiedBit = std::uint64_t{1} << 62;
    static constexpr std::uint64_t kDataPublishedBit = std::uint64_t{1} << 61;
    static constexpr std::uint64_t kCycleMask = (std::uint64_t{1} << 61) - 1;

    [[nodiscard, gnu::const]] static constexpr std::uint64_t pack_state(std::uint64_t cycle, bool safe, bool occupied,
                                                                        bool published = false) noexcept {
        return (safe ? kIsSafeBit : 0) | (occupied ? kOccupiedBit : 0) | (published ? kDataPublishedBit : 0)
             | (cycle & kCycleMask);
    }

    [[nodiscard, gnu::const]] static constexpr std::uint64_t cycle_of(std::uint64_t s) noexcept {
        return s & kCycleMask;
    }
    [[nodiscard, gnu::const]] static constexpr bool is_safe(std::uint64_t s) noexcept { return (s & kIsSafeBit) != 0; }
    [[nodiscard, gnu::const]] static constexpr bool is_occupied(std::uint64_t s) noexcept {
        return (s & kOccupiedBit) != 0;
    }
    [[nodiscard, gnu::const]] static constexpr bool is_data_published(std::uint64_t s) noexcept {
        return (s & kDataPublishedBit) != 0;
    }

    // One cell per cache line.  The published algorithm packs cells and
    // permutes the index to spread contended state words across lines.
    // Aligning each cell instead trades padding for the permutation's
    // arithmetic, and is cache-optimal for any T that fits the line.

    struct alignas(64) Cell {
        std::atomic<std::uint64_t> state{pack_state(0, true, false)};
        T data{};
    };

    alignas(64) std::array<Cell, kCells> cells_;

    // Both start at kCells so the first enqueue computes cycle 1
    // against a cell whose initial cycle is 0, and therefore wins its
    // first CAS.  Neither counter ever decrements: a push claims a
    // fresh tail ticket, a pop claims a fresh head ticket.
    alignas(64) safety::AtomicMonotonic<std::uint64_t> tail_{kCells};
    alignas(64) safety::AtomicMonotonic<std::uint64_t> head_{kCells};

    // A bare atomic rather than AtomicMonotonic.  A failed dequeue must
    // decrement this counter, which a monotonic wrapper would forbid.
    alignas(64) std::atomic<std::int64_t> threshold_{-1};

    // Diagnostic only.  Bumped on the rare wasted-ticket path, so its
    // stronger-than-needed ordering never lands on a hot iteration.
    alignas(64) safety::AtomicMonotonic<std::uint64_t> enqueue_ticket_waste_{0};

public:
    MpmcRing() noexcept = default;

    // Returns false if the ring is full, or if no unoccupied cell turns
    // up within 2 × Capacity ticket hops.
    [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
        {
            const std::uint64_t t_snap = tail_.get();
            const std::uint64_t h_snap = head_.get();
            if (t_snap >= h_snap + kCells) [[unlikely]] {
                return false;
            }
        }

        for (std::size_t outer_retry = 0;; ++outer_retry) {
            if (outer_retry > kCells) [[unlikely]]
                return false;

            const std::uint64_t T_ = tail_.bump_by(1);
            const std::uint64_t j = T_ & kMask;
            const std::uint64_t cycle_T = T_ >> std::countr_zero(kCells);

            Cell& cell = cells_[j];
            std::uint64_t ent = cell.state.load(std::memory_order_acquire);

            for (;;) {
                const std::uint64_t ent_cycle = cycle_of(ent);

                if (ent_cycle < cycle_T && !is_occupied(ent) && (is_safe(ent) || head_.get() <= T_)) {
                    // Exactly one producer wins this CAS for a given
                    // cycle and cell, and becomes the sole writer of
                    // cell.data for that cycle.  Losers re-claim a
                    // ticket.  Without the reservation the inlined
                    // payload races between producers at different
                    // cycles that map to this cell.
                    const std::uint64_t reserved_ent = pack_state(cycle_T,
                                                                  /*safe*/ true,
                                                                  /*occupied*/ true,
                                                                  /*published*/ false);

                    if (!cell.state.compare_exchange_strong(ent, reserved_ent, std::memory_order_acq_rel,
                                                            std::memory_order_acquire)) {
                        continue;
                    }

                    cell.data = item;

                    // fetch_or leaves IsSafe, Occupied and Cycle
                    // untouched.  A consumer at a higher cycle can
                    // clear IsSafe between the reservation above and
                    // this publish, and a plain store would undo that.
                    (void)cell.state.fetch_or(kDataPublishedBit, std::memory_order_release);

                    // No postcondition belongs here.  The obvious
                    // `threshold_ == kThresholdHi` is false: consumers
                    // may decrement between the store and any read.
                    // The guarantee is temporal, not sequential.
                    if (threshold_.load(std::memory_order_acquire) != kThresholdHi) {
                        threshold_.store(kThresholdHi, std::memory_order_release);
                    }
                    return true;
                }
                (void)enqueue_ticket_waste_.bump_by(1);
                break;
            }
        }
    }

    // Returns nullopt if the ring is empty, or if the scan finds no
    // ready cell before the threshold runs out.
    [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
        if (threshold_.load(std::memory_order_acquire) < 0) [[unlikely]] {
            return std::nullopt;
        }

        for (std::size_t outer_retry = 0;; ++outer_retry) {
            if (outer_retry > kCells) [[unlikely]]
                return std::nullopt;

            const std::uint64_t H_ = head_.bump_by(1);
            const std::uint64_t j = H_ & kMask;
            const std::uint64_t cycle_H = H_ >> std::countr_zero(kCells);

            Cell& cell = cells_[j];
            std::uint64_t ent = cell.state.load(std::memory_order_acquire);

            for (;;) {
                const std::uint64_t ent_cycle = cycle_of(ent);

                if (ent_cycle == cycle_H) {
                    // The producer may have reserved this cell without
                    // yet publishing, so cell.data is not readable
                    // until DataPublished is set.  Ticket uniqueness
                    // means at most one consumer waits here, on one
                    // producer.
                    //
                    // The wait escalates from pause to yield because
                    // the producer can be descheduled between its
                    // reservation and its publish.  Pausing alone then
                    // burns this core for a whole scheduling quantum,
                    // while yielding lets the producer run.  Pause
                    // still covers the common intra-core case.
                    constexpr std::size_t kPauseBeforeYield = 64;
                    std::size_t spin_iters = 0;
                    while (!is_data_published(ent)) [[unlikely]] {
                        if (spin_iters < kPauseBeforeYield) {
                            CRUCIBLE_SPIN_PAUSE;
                            ++spin_iters;
                        } else {
                            std::this_thread::yield();
                        }
                        ent = cell.state.load(std::memory_order_acquire);
                    }

                    T result = cell.data;
                    // DataPublished must go back to zero so the next
                    // producer at this cell starts its reserve-then-
                    // publish sequence from a clean state.
                    const std::uint64_t clear_mask = ~(kOccupiedBit | kDataPublishedBit);
                    (void)cell.state.fetch_and(clear_mask, std::memory_order_acq_rel);
                    return result;
                }

                if (ent_cycle < cycle_H) {
                    std::uint64_t new_ent;
                    if (!is_occupied(ent)) {
                        // An empty cell moves forward to this cycle.
                        new_ent = pack_state(cycle_H, is_safe(ent), false, /*published*/ false);
                    } else {
                        // An occupied cell keeps its cycle and becomes
                        // unsafe.  DataPublished must survive: the
                        // producer at that older cycle may already have
                        // published, and the consumer holding the
                        // matching older head ticket still has to read
                        // the payload.
                        new_ent = pack_state(ent_cycle, false, true, is_data_published(ent));
                    }

                    if (cell.state.compare_exchange_strong(ent, new_ent, std::memory_order_acq_rel,
                                                           std::memory_order_acquire)) {
                        break;
                    }
                    continue;
                }

                break;
            }

            // A tail at or below this ticket means no producer has
            // committed past us, so the ring is empty.
            const std::uint64_t T_ = tail_.get();
            if (T_ <= H_ + 1) {
                threshold_.fetch_sub(1, std::memory_order_acq_rel);
                return std::nullopt;
            }

            if (threshold_.fetch_sub(1, std::memory_order_acq_rel) <= 0) {
                return std::nullopt;
            }
        }
    }

    // The batch form is ergonomic, not a throughput optimization: every
    // item still pays a ticket claim and a cell CAS.  Prefer try_push
    // where the caller wants per-item retry control.
    [[nodiscard, gnu::hot]] std::size_t try_push_batch(std::span<const T> items) noexcept {
        std::size_t pushed = 0;
        for (const T& item : items) {
            if (!try_push(item)) break;
            ++pushed;
        }
        return pushed;
    }

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

    [[nodiscard]] std::size_t size_approx() const noexcept {
        const std::uint64_t t = tail_.get();
        const std::uint64_t h = head_.get();
        return t > h ? (t - h) : 0;
    }

    [[nodiscard]] bool empty_approx() const noexcept { return threshold_.load(std::memory_order_acquire) < 0; }

    [[nodiscard]] std::int64_t threshold_snapshot() const noexcept {
        return threshold_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t ticket_waste_count() const noexcept { return enqueue_ticket_waste_.get(); }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    [[nodiscard]] static constexpr std::size_t internal_cells() noexcept { return kCells; }
};

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
