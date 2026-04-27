#pragma once

// ═══════════════════════════════════════════════════════════════════
// BitmapMpscRing<T, Capacity> — MPSC ring with out-of-band metadata
//
// The "beyond Vyukov" design.  Cells are pure data (8 bytes each for
// uint64); ready/empty signal lives in a separate bitmap (1 bit per
// cell).  This is the layout the literature should have used but
// didn't, because the published MPMC algorithms (Vyukov 2007, LCRQ
// 2013, SCQ 2019) all stash per-cell metadata INLINE with the cell —
// forcing 16-byte minimum cell size + cache-line padding for false-
// sharing protection (= 64 byte cells).  At N=1024 batches that's a
// 64 KB working set vs SPSC's 8 KB — exceeds L1d, dominates throughput.
//
// ─── The bitmap fix ────────────────────────────────────────────────
//
// Cells stay packed at sizeof(T).  A separate bitmap (1 bit per cell,
// stored in a few atomic<uint64_t> words) tracks readiness:
//
//   bit K = 0  : cell K is empty (no published data)
//   bit K = 1  : cell K holds published data
//
// For 1024 cells × uint64: 8 KB cells + 16 × 8 = 128 B bitmap = 8.125
// KB working set.  Same density as SPSC.
//
// ─── The cross-round synchronization (the hard part) ──────────────
//
// With 1 bit per cell, the bit doesn't encode "round" — it just says
// "currently published or not."  The consumer's tail counter
// implicitly tracks round (tail / Capacity = round number).
//
// The hazard: producer P2 at round R+1, cell K must NOT race with
// producer P1's round R, cell K — both write the same physical cell.
// In Vyukov, per-cell sequence number prevents this (P2 waits for
// sequence to advance past R*Capacity + K).
//
// Without per-cell sequence, we use the CAPACITY GATE itself as the
// synchronization primitive:
//
//   Producer at pos = K + Capacity (round R+1) checks:
//     pos + 1 - tail <= Capacity  ⇒  tail >= K + 1
//
//   For tail to reach K + 1, the consumer must have:
//     1. Read bit K = 1 (P1 published)
//     2. Read cells_[K] (acquired round-R data)
//     3. Cleared bit K via fetch_and(~mask, release)
//     4. Advanced tail to K + 1 via store(release)
//
//   By happens-before chain on tail (P2's tail.get(acquire) sees
//   consumer's tail.store(release)), the consumer's bit-clear (which
//   happened-before tail.store) is visible to P2 BEFORE P2's bit-set.
//
//   Result: bit K transitions 0 → 1 (P1) → 0 (consumer) → 1 (P2),
//   each on a distinct atomic op.  No race.
//
// ─── Per-call atomic budget ────────────────────────────────────────
//
//   try_push (single):
//     1 acquire-load on tail (capacity check)
//     1 weak CAS on head (claim 1 ticket)
//     1 plain data write
//     1 fetch_or (1 bit) on bitmap
//
//   try_push_batch<N>:
//     1 acquire-load on tail
//     1 weak CAS on head (claim N tickets)
//     N data writes (pure memcpy through L1d store buffer)
//     ⌈N/64⌉ fetch_or ops on bitmap (release; sets up to 64 bits at once)
//
//     Per-item cost: O(1/N) atomic ops + O(1) data store.
//     For N=1024: 16 fetch_or + 1024 stores ≈ 0.07-0.15 ns/item
//     (within ~2× of SPSC's 0.076 ns/item floor).
//
//   try_pop (single consumer ONLY):
//     1 acquire-load on bitmap word
//     1 plain data read
//     1 fetch_and (clear bit) — release
//     1 plain store on tail (release on x86, free on TSO)
//
//   try_pop_batch<N>:
//     1 acquire-load on bitmap word(s)
//     scan for ready prefix R
//     R plain data reads
//     ⌈R/64⌉ fetch_and ops on bitmap
//     1 plain store on tail
//
// ─── Constraints ───────────────────────────────────────────────────
//
//   * T trivially-copyable + trivially-destructible (per-cell direct
//     assignment, no constructor races).
//   * Capacity must be a power of two AND >= 64 (bitmap word
//     alignment; smaller capacity could be supported but not needed
//     in production).
//   * Single consumer ONLY (caller MUST guarantee).  For multi-
//     consumer, see future BitmapMpmcRing variant.
//   * Batched API: caller passes std::span<const T> for push or
//     std::span<T> for pop.  Batches need not align to bitmap word
//     boundaries — internal logic handles partial words.
//
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

// ── BitmapRingValue<T> concept ─────────────────────────────────────
//
// T must be trivially copyable / destructible because cells are
// direct-stored (no atomic per cell), and the bitmap-protected
// publish/consume protocol relies on the data write completing
// before the bit-set release.

template <typename T>
concept BitmapRingValue =
    std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T>;

// ── BitmapMpscRing<T, Capacity> ────────────────────────────────────

template <BitmapRingValue T, std::size_t Capacity>
class BitmapMpscRing : public safety::Pinned<BitmapMpscRing<T, Capacity>> {
public:
    static_assert(std::has_single_bit(Capacity),
                  "Capacity must be a power of two");
    static_assert(Capacity >= 64,
                  "Capacity must be >= 64 (bitmap word alignment)");

private:
    static constexpr std::uint64_t MASK  = std::uint64_t{Capacity - 1};
    static constexpr std::size_t   WORDS = Capacity / 64;
    static constexpr std::uint64_t kAllSet = ~std::uint64_t{0};

public:
    BitmapMpscRing() noexcept = default;

    // ── try_push (single item) ────────────────────────────────────
    //
    // Claim head via weak CAS, write cell, set bit.  Returns false
    // iff the queue is full (tail hasn't drained enough).
    //
    // Memory ordering:
    //   - tail_.get(acquire): pairs with consumer's tail.store(release).
    //     This sync edge ensures the consumer's bit-clear (which
    //     happened-before tail-advance) is visible to our bit-set.
    //   - head_.compare_exchange_advance_weak(relaxed, relaxed):
    //     ticket claim only; no data sync needed (the bit-set later
    //     handles publish ordering).
    //   - cell store: non-atomic; we exclusively own the cell from
    //     CAS-success until bit-set.
    //   - ready_.fetch_or(release): publishes the data write to the
    //     consumer's acquire-load on the bitmap.

    [[nodiscard, gnu::hot]] bool try_push(T item) noexcept {
        for (;;) {
            const std::uint64_t pos = head_.peek_relaxed();
            const std::uint64_t tail_val = tail_.get();
            // Capacity check: pos + 1 - tail_val <= Capacity.
            // Equivalent (avoid overflow): pos - tail_val < Capacity.
            if (pos - tail_val >= Capacity) [[unlikely]] {
                return false;
            }
            std::uint64_t expected = pos;
            if (!head_.compare_exchange_advance_weak(expected, pos + 1,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                // Another producer raced; retry.
                continue;
            }

            // We own slot at pos.  Write data, then publish via bit-set.
            const std::size_t cell_idx = pos & MASK;
            cells_[cell_idx] = item;

            const std::size_t word_idx = cell_idx >> 6;        // /64
            const std::size_t bit_idx  = cell_idx & 63;        // %64
            const std::uint64_t mask   = std::uint64_t{1} << bit_idx;
            ready_[word_idx].fetch_or(mask, std::memory_order_release);
            return true;
        }
    }

    // ── try_push_batch (any producer, batched) ────────────────────
    //
    // Claim N tickets in ONE weak CAS, write N data cells, publish
    // via fetch_or per-bitmap-word.  All-or-nothing: either all N
    // items push or zero (queue full or wraparound rejection).
    //
    // Per-batch atomic budget:
    //   1 tail acquire-load + 1 head weak CAS + N data writes +
    //   ⌈N/64⌉ bitmap fetch_or ops.
    //
    // For wraparound batches (cells span buffer end), the bitmap
    // publish is split into two segments by publish_range_.

    [[nodiscard, gnu::hot]] std::size_t try_push_batch(
        std::span<const T> items) noexcept {
        const std::size_t N = items.size();
        if (N == 0) return 0;
        if (N > Capacity) [[unlikely]] return 0;

        for (;;) {
            const std::uint64_t pos = head_.peek_relaxed();
            const std::uint64_t tail_val = tail_.get();
            // Capacity check: pos + N - tail_val <= Capacity.
            if (pos + N - tail_val > Capacity) [[unlikely]] {
                return 0;
            }
            std::uint64_t expected = pos;
            if (!head_.compare_exchange_advance_weak(expected, pos + N,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                continue;
            }

            // We own [pos, pos + N).  Write all data, then publish
            // bits via fetch_or per word.
            for (std::size_t i = 0; i < N; ++i) {
                cells_[(pos + i) & MASK] = items[i];
            }
            publish_range_(pos & MASK, ((pos + N - 1) & MASK) + 1, N);
            return N;
        }
    }

    // ── try_pop (single consumer ONLY) ────────────────────────────
    //
    // Caller MUST guarantee no concurrent try_pop or try_pop_batch.
    // Returns nullopt iff the queue is empty (bit clear at tail's
    // cell).
    //
    // Memory ordering:
    //   - ready_.load(acquire): pairs with producer's fetch_or(release)
    //     so the data write is visible after we observe the set bit.
    //   - cell read: non-atomic; happens-after the acquire above.
    //   - ready_.fetch_and(release): clears the bit; the release
    //     pairs with the next-round producer's fetch_or(release)
    //     atomic-RMW chain (atomic OR / AND on same word are
    //     sequentially consistent).
    //   - tail_.store(release): publishes "consumer has consumed
    //     position pos" to producer's tail.get(acquire) capacity
    //     check.  Ordering: bit-clear → tail-advance ensures the
    //     next-round producer (waiting on capacity) sees the cleared
    //     bit when it observes the new tail.

    [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
        const std::uint64_t pos = tail_.peek_relaxed();
        const std::size_t cell_idx = pos & MASK;
        const std::size_t word_idx = cell_idx >> 6;
        const std::size_t bit_idx  = cell_idx & 63;
        const std::uint64_t mask   = std::uint64_t{1} << bit_idx;

        const std::uint64_t word = ready_[word_idx].load(std::memory_order_acquire);
        if ((word & mask) == 0) {
            return std::nullopt;
        }

        const T item = cells_[cell_idx];
        // Clear bit — release pairs with next-round producer's
        // fetch_or on the same word.
        ready_[word_idx].fetch_and(~mask, std::memory_order_release);
        // Advance tail — release pairs with next-round producer's
        // tail.get(acquire) capacity check.
        tail_.store(pos + 1, std::memory_order_release);
        return item;
    }

    // ── try_pop_batch (single consumer ONLY) ──────────────────────
    //
    // Drain up to out.size() items.  Scans the bitmap for the
    // longest contiguous ready prefix starting at tail, drains it
    // all at once, then advances tail by exactly that prefix length.
    //
    // Returns the number of items popped (0 iff queue empty at
    // tail's cell).  Items written into out[0..return-1] in FIFO
    // order.
    //
    // Per-batch atomic budget:
    //   1 to ceil(N/64) bitmap loads (acquire) for the scan +
    //   ceil(R/64) fetch_and ops (release) for clearing +
    //   1 tail store (release).

    [[nodiscard, gnu::hot]] std::size_t try_pop_batch(
        std::span<T> out) noexcept {
        const std::size_t cap = out.size();
        if (cap == 0) return 0;

        const std::uint64_t pos0 = tail_.peek_relaxed();

        // Scan: read each bit in order, count the contiguous ready
        // prefix.  Optimization: read whole bitmap word and use
        // bit-trick to count trailing ones.
        std::size_t R = 0;
        while (R < cap) {
            const std::size_t cell_idx = (pos0 + R) & MASK;
            const std::size_t word_idx = cell_idx >> 6;
            const std::size_t bit_idx  = cell_idx & 63;

            // Read word once, scan as many bits as we can within it.
            const std::uint64_t word =
                ready_[word_idx].load(std::memory_order_acquire);
            // Shift right so our bit_idx becomes bit 0, then count
            // trailing ones via inversion + countr_zero.
            const std::uint64_t shifted = word >> bit_idx;
            // Number of consecutive 1s starting at our bit:
            //   if shifted == ~0 (all 1s remaining): 64 - bit_idx
            //   else: countr_zero(~shifted)
            const std::size_t avail_in_word = (~shifted == 0)
                ? (64 - bit_idx)
                : static_cast<std::size_t>(std::countr_zero(~shifted));
            // Cap by remaining buffer in word + remaining requested.
            const std::size_t remaining = cap - R;
            const std::size_t take = std::min(avail_in_word, remaining);
            if (take == 0) break;
            R += take;
            // If we consumed less than the word's available, we hit
            // a 0 bit — stop here.
            if (take < avail_in_word) break;
            // Otherwise, we ran off the end of this word; loop
            // continues to the next word.
        }

        if (R == 0) return 0;

        // Read R cells of data.
        for (std::size_t i = 0; i < R; ++i) {
            out[i] = cells_[(pos0 + i) & MASK];
        }
        // Clear R bits via per-word fetch_and.
        clear_range_(pos0 & MASK, ((pos0 + R - 1) & MASK) + 1, R);
        // Advance tail by R — release pairs with producer's
        // tail.get(acquire).
        tail_.store(pos0 + R, std::memory_order_release);
        return R;
    }

    // ── empty_approx (any thread, NOT exact) ──────────────────────
    [[nodiscard]] bool empty_approx() const noexcept {
        const std::uint64_t pos = tail_.get();
        const std::size_t cell_idx = pos & MASK;
        const std::size_t word_idx = cell_idx >> 6;
        const std::size_t bit_idx  = cell_idx & 63;
        const std::uint64_t mask   = std::uint64_t{1} << bit_idx;
        return (ready_[word_idx].load(std::memory_order_acquire) & mask) == 0;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    // ── publish_range_ — set bits [bit_start, bit_end) in bitmap ──
    //
    // Handles wraparound: if N items span the buffer end, sets two
    // disjoint bit ranges.  Per-word fetch_or with release.

    void publish_range_(std::size_t bit_start, std::size_t bit_end,
                        std::size_t N) noexcept {
        if (N >= Capacity) [[unlikely]] {
            // Full-buffer publish: set every bit.
            for (auto& w : ready_) {
                w.fetch_or(kAllSet, std::memory_order_release);
            }
            return;
        }
        // Wraparound case: bit_end <= bit_start (modulo wrap)
        if (bit_end <= bit_start) {
            // Two segments: [bit_start, Capacity) + [0, bit_end)
            set_word_range_(bit_start, Capacity);
            set_word_range_(0, bit_end);
        } else {
            set_word_range_(bit_start, bit_end);
        }
    }

    // Helper: set bits [start, end) in bitmap.  start < end
    // guaranteed; both within [0, Capacity).
    void set_word_range_(std::size_t start, std::size_t end) noexcept {
        while (start < end) {
            const std::size_t word_idx = start >> 6;
            const std::size_t bit_offset = start & 63;
            const std::size_t bits_in_word =
                std::min<std::size_t>(64 - bit_offset, end - start);
            const std::uint64_t mask = (bits_in_word == 64)
                ? kAllSet
                : (((std::uint64_t{1} << bits_in_word) - 1) << bit_offset);
            ready_[word_idx].fetch_or(mask, std::memory_order_release);
            start += bits_in_word;
        }
    }

    // ── clear_range_ — clear bits [bit_start, bit_end) ────────────
    void clear_range_(std::size_t bit_start, std::size_t bit_end,
                      std::size_t N) noexcept {
        if (N >= Capacity) [[unlikely]] {
            for (auto& w : ready_) {
                w.fetch_and(0, std::memory_order_release);
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
            const std::size_t bits_in_word =
                std::min<std::size_t>(64 - bit_offset, end - start);
            const std::uint64_t mask = (bits_in_word == 64)
                ? kAllSet
                : (((std::uint64_t{1} << bits_in_word) - 1) << bit_offset);
            ready_[word_idx].fetch_and(~mask, std::memory_order_release);
            start += bits_in_word;
        }
    }

    // ── Storage layout ────────────────────────────────────────────
    //
    // Cells: pure T, alignas(64) on the array (not per-cell).  For
    // T=uint64 and Capacity=1024, this is 8 KB on a single L1d-
    // friendly contiguous region.
    //
    // Bitmap: WORDS atomic<uint64_t>, each on the same cache line for
    // small Capacity, separate cache lines for large.  Each word
    // covers 64 cells.  Producer fetch_or, consumer fetch_and on the
    // same word are atomic-RMW; safe but contend on the cache line.
    //
    // head_ and tail_: each on its own cache line (alignas(64)) to
    // prevent cross-thread false sharing on counter writes.

    alignas(64) std::array<T, Capacity>                         cells_{};
    alignas(64) std::array<std::atomic<std::uint64_t>, WORDS>   ready_{};
    alignas(64) safety::AtomicMonotonic<std::uint64_t>          head_{0};
    alignas(64) safety::AtomicMonotonic<std::uint64_t>          tail_{0};
};

}  // namespace crucible::concurrent
