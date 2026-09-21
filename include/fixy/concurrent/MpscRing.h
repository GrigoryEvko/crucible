#pragma once

// Multi-producer single-consumer ring.  A cell holds only T.  The ready
// signal lives out of band, one bit per cell in a packed bitmap.
//
// The rejected alternative is a per-cell sequence number, which forces
// every cell onto its own cache line and pushes the ring out of L1d at
// useful capacities.  The bitmap leaves the cells packed.
//
// One bit cannot encode a round, so nothing in a cell distinguishes a
// producer at round R from a producer at round R + 1 on the same cell
// K.  The capacity gate supplies the missing order.  A producer at
// position K + Capacity passes the gate only when tail >= K + 1, which
// means the consumer has already cleared bit K with a release fetch_and
// and then advanced tail with a release store.  The producer's acquire
// load of tail synchronizes with that store, so the clear is visible
// before the producer's set.  Bit K therefore moves 0, 1, 0, 1 in that
// order, each step a distinct atomic operation.
//
// Old spelling: include/crucible/concurrent/_MpscRing.h.

#include <fixy/Mutation.h>
#include <fixy/concurrent/RingValue.h>

#include <foundation/Pinned.h>
#include <foundation/Platform.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace fixy::concurrent {

template <RingValue T, std::size_t Capacity>
class MpscRing : public ::foundation::Pinned<MpscRing<T, Capacity>> {
public:
    using value_type = T;
    static constexpr std::size_t channel_capacity = Capacity;

    static_assert(std::has_single_bit(Capacity), "fixy::concurrent::MpscRing<T, Capacity>: Capacity must be a power "
                                                 "of two.  The position-to-cell map is a bitwise AND with "
                                                 "Capacity - 1, which is the index modulo Capacity only when "
                                                 "Capacity is a power of two.");
    static_assert(Capacity >= 1, "fixy::concurrent::MpscRing<T, Capacity>: Capacity must be greater than zero.  A "
                                 "ring with no cells has no bit for a producer to set.");

private:
    static constexpr std::uint64_t MASK = std::uint64_t{Capacity - 1};
    // A Capacity below 64 still gets one word.  Its unused high bits
    // stay zero, because every index derives from a masked position and
    // so is always below Capacity.
    static constexpr std::size_t WORDS = (Capacity + 63) / 64;
    static constexpr std::uint64_t kAllSet = ~std::uint64_t{0};

public:
    MpscRing() noexcept = default;

    // The head CAS is relaxed because it claims a ticket and carries no
    // payload.  The release on the bitmap set is what publishes the
    // cell write, and the caller owns the cell from CAS success until
    // that set.

    [[nodiscard, gnu::hot]] bool try_push(T item) noexcept {
        for (;;) {
            // load_relaxed, not peek_relaxed: producers race-CAS a
            // shared head_, so the sole-writer claim behind
            // peek_relaxed does not hold here.
            const std::uint64_t pos = head_.load_relaxed();
            const std::uint64_t tail_val = tail_.get();
            // The gate is pos + 1 - tail_val <= Capacity, rewritten to
            // subtract first so it cannot overflow.
            if (pos - tail_val >= Capacity) [[unlikely]] {
                return false;
            }
            std::uint64_t expected = pos;
            if (!head_.compare_exchange_advance_weak(expected, pos + 1, std::memory_order_relaxed,
                                                     std::memory_order_relaxed)) {
                continue;
            }

            const std::size_t cell_idx = pos & MASK;
            cells_[cell_idx] = item;

            const std::size_t word_idx = cell_idx >> 6;
            const std::size_t bit_idx = cell_idx & 63;
            const std::uint64_t mask = std::uint64_t{1} << bit_idx;
            ready_[word_idx].fetch_or(mask, std::memory_order_release);
            return true;
        }
    }

    // All or nothing: a batch that does not fit pushes no items.

    [[nodiscard, gnu::hot]] std::size_t try_push_batch(std::span<const T> items) noexcept {
        const std::size_t N = items.size();
        if (N == 0) return 0;
        if (N > Capacity) [[unlikely]]
            return 0;

        for (;;) {
            const std::uint64_t pos = head_.load_relaxed();
            const std::uint64_t tail_val = tail_.get();
            if (pos + N - tail_val > Capacity) [[unlikely]] {
                return 0;
            }
            std::uint64_t expected = pos;
            if (!head_.compare_exchange_advance_weak(expected, pos + N, std::memory_order_relaxed,
                                                     std::memory_order_relaxed)) {
                continue;
            }

            for (std::size_t i = 0; i < N; ++i) {
                cells_[(pos + i) & MASK] = items[i];
            }
            publish_range_(pos & MASK, ((pos + N - 1) & MASK) + 1, N);
            return N;
        }
    }

    // The caller guarantees no concurrent try_pop or try_pop_batch.
    // The acquire load of the bitmap pairs with the producer's release
    // fetch_or, so the cell read below sees the published payload.

    [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
        const std::uint64_t pos = tail_.peek_relaxed();
        const std::size_t cell_idx = pos & MASK;
        const std::size_t word_idx = cell_idx >> 6;
        const std::size_t bit_idx = cell_idx & 63;
        const std::uint64_t mask = std::uint64_t{1} << bit_idx;

        const std::uint64_t word = ready_[word_idx].load(std::memory_order_acquire);
        if ((word & mask) == 0) {
            return std::nullopt;
        }

        const T item = cells_[cell_idx];
        // Clear the bit before advancing tail.  A producer that then
        // passes the capacity gate observes the new tail and therefore
        // the cleared bit.
        ready_[word_idx].fetch_and(~mask, std::memory_order_release);
        tail_.store(pos + 1, std::memory_order_release);
        return item;
    }

    // The caller guarantees no concurrent try_pop or try_pop_batch.

    [[nodiscard, gnu::hot]] std::size_t try_pop_batch(std::span<T> out) noexcept {
        // The ring holds at most Capacity live items, so a request
        // larger than Capacity has to be clamped.  Unclamped, the
        // prefix scan below wraps past the buffer end, re-reads the
        // still-set bits of the cells it already counted, and grows R
        // beyond Capacity.  The drain then returns duplicates, clears
        // every bit, and leaves tail ahead of head, at which point the
        // producer's unsigned gate underflows and reads as full
        // forever.  Clamping rather than rejecting keeps the
        // partial-fill contract.
        const std::size_t cap = std::min<std::size_t>(out.size(), Capacity);
        if (cap == 0) return 0;

        const std::uint64_t pos0 = tail_.peek_relaxed();

        std::size_t R = 0;
        while (R < cap) {
            const std::size_t cell_idx = (pos0 + R) & MASK;
            const std::size_t word_idx = cell_idx >> 6;
            const std::size_t bit_idx = cell_idx & 63;

            const std::uint64_t word = ready_[word_idx].load(std::memory_order_acquire);
            const std::uint64_t shifted = word >> bit_idx;
            // countr_zero of an all-zero inversion answers 64, but only
            // the top 64 - bit_idx bits of this word belong to the
            // scan, so the all-ones case needs its own arm.
            const std::size_t avail_in_word =
                (~shifted == 0) ? (64 - bit_idx) : static_cast<std::size_t>(std::countr_zero(~shifted));
            const std::size_t remaining = cap - R;
            const std::size_t take = std::min(avail_in_word, remaining);
            if (take == 0) break;
            R += take;
            // Stopping short of the word's run means a clear bit ended
            // the prefix.  Consuming the whole run continues into the
            // next word.
            if (take < avail_in_word) break;
        }

        if (R == 0) return 0;

        for (std::size_t i = 0; i < R; ++i) {
            out[i] = cells_[(pos0 + i) & MASK];
        }
        clear_range_(pos0 & MASK, ((pos0 + R - 1) & MASK) + 1, R);
        tail_.store(pos0 + R, std::memory_order_release);
        return R;
    }

    [[nodiscard]] bool empty_approx() const noexcept {
        const std::uint64_t pos = tail_.get();
        const std::size_t cell_idx = pos & MASK;
        const std::size_t word_idx = cell_idx >> 6;
        const std::size_t bit_idx = cell_idx & 63;
        const std::uint64_t mask = std::uint64_t{1} << bit_idx;
        return (ready_[word_idx].load(std::memory_order_acquire) & mask) == 0;
    }

    // The two loads are not one snapshot, so head can outrun the tail
    // that was read for it.  The clamp absorbs that.
    [[nodiscard]] std::size_t size_approx() const noexcept {
        const std::uint64_t h = head_.get();
        const std::uint64_t t = tail_.get();
        const std::uint64_t d = (h > t) ? (h - t) : 0;
        return d > Capacity ? Capacity : static_cast<std::size_t>(d);
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    void publish_range_(std::size_t bit_start, std::size_t bit_end, std::size_t N) noexcept {
        if (N >= Capacity) [[unlikely]] {
            for (auto& word : ready_) {
                word.fetch_or(kAllSet, std::memory_order_release);
            }
            return;
        }
        // A range that wrapped the buffer end arrives with its end at
        // or below its start, and splits into two segments.
        if (bit_end <= bit_start) {
            set_word_range_(bit_start, Capacity);
            set_word_range_(0, bit_end);
        } else {
            set_word_range_(bit_start, bit_end);
        }
    }

    // Callers guarantee start < end and both within [0, Capacity).
    void set_word_range_(std::size_t start, std::size_t end) noexcept {
        while (start < end) {
            const std::size_t word_idx = start >> 6;
            const std::size_t bit_offset = start & 63;
            const std::size_t bits_in_word = std::min<std::size_t>(64 - bit_offset, end - start);
            const std::uint64_t mask =
                (bits_in_word == 64) ? kAllSet : (((std::uint64_t{1} << bits_in_word) - 1) << bit_offset);
            ready_[word_idx].fetch_or(mask, std::memory_order_release);
            start += bits_in_word;
        }
    }

    void clear_range_(std::size_t bit_start, std::size_t bit_end, std::size_t N) noexcept {
        if (N >= Capacity) [[unlikely]] {
            for (auto& word : ready_) {
                word.fetch_and(0, std::memory_order_release);
            }
            return;
        }
        if (bit_end <= bit_start) {
            clear_word_range_(bit_start, Capacity);
            clear_word_range_(0, bit_end);
        } else {
            clear_word_range_(bit_start, bit_end);
        }
    }

    void clear_word_range_(std::size_t start, std::size_t end) noexcept {
        while (start < end) {
            const std::size_t word_idx = start >> 6;
            const std::size_t bit_offset = start & 63;
            const std::size_t bits_in_word = std::min<std::size_t>(64 - bit_offset, end - start);
            const std::uint64_t mask =
                (bits_in_word == 64) ? kAllSet : (((std::uint64_t{1} << bits_in_word) - 1) << bit_offset);
            ready_[word_idx].fetch_and(~mask, std::memory_order_release);
            start += bits_in_word;
        }
    }

    // The array is aligned, not each cell, so the cells stay packed.
    // A producer fetch_or and the consumer fetch_and on one bitmap word
    // are read-modify-writes and so are safe, but they do contend for
    // the line.  head_ and tail_ each take a line of their own, which
    // AtomicMonotonic carries: it is alignas(64) and at least a line in
    // size, and asserts both in its own header.

    alignas(64) std::array<T, Capacity> cells_{};
    alignas(64) std::array<std::atomic<std::uint64_t>, WORDS> ready_{};
    ::fixy::AtomicMonotonic<std::uint64_t> head_ = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
    ::fixy::AtomicMonotonic<std::uint64_t> tail_ = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
};

}  // namespace fixy::concurrent
