#pragma once

// ═══════════════════════════════════════════════════════════════════
// SpscRing<T, Capacity> — single-producer single-consumer bounded ring
//
// The simplest concurrent primitive: one producer pushes, one
// consumer pops, lock-free, bounded.  No CAS — just acquire/release
// atomic loads on head/tail (each on its own cache line) plus one
// cell store/load per call.
//
// Trades flexibility for speed: no MPMC support, no batched drain
// (TraceRing has those for its specific Entry layout); just clean
// SPSC for any trivially-copyable T.  Use as the underlying cell
// type for ShardedSpscGrid (M×N decomposition of MPMC into SPSC),
// or directly as a thread-pair channel.
//
// ─── When to use SpscRing vs other primitives ───────────────────────
//
//   SpscRing<T, N>:       ONE producer, ONE consumer — both threads
//                         pinned for the ring's lifetime.  Cheapest
//                         possible concurrent queue.
//
//   MpscRing<T, N>:       MANY producers, ONE consumer.  Per-cell
//                         sequence number protocol; producer pays
//                         one CAS on the global head per push.  Use
//                         when producer count is dynamic.
//
//   ShardedSpscGrid:      M producers × N consumers via M×N SpscRings.
//                         Stays at SPSC speed by sharding instead of
//                         going MPMC.  M, N compile-time fixed.
//
//   ChaseLevDeque:        ONE owner + many thieves (work-stealing).
//                         For fork-join thread pools.
//
//   AtomicSnapshot:       Latest-value publication.  NOT a queue —
//                         readers see only the most recent T.
//
//   TraceRing (specific): SPSC with Entry + 3 parallel arrays for
//                         the bg drain pipeline.  Don't reuse for
//                         general SPSC — its 5 MB footprint is too
//                         expensive for arbitrary channels.
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * T trivially-copyable + trivially-destructible (cell stores
//     T directly; no constructor races)
//   * Capacity must be a power of two, > 0
//   * Producer thread is the sole writer of head_ + buffer_ slots
//     in [tail, head); consumer thread is the sole writer of tail_
//     and sole reader of slots in [tail, head).  Caller MUST
//     guarantee this — there's no runtime check.
//
// ─── Per-call atomic shape ──────────────────────────────────────────
//
//   try_push: 1 acquire-load on tail, 1 cell store, 1 release-store
//             on head
//   try_pop:  1 acquire-load on head, 1 cell load, 1 release-store
//             on tail
//   try_push when full:  load tail, fail-fast
//   try_pop  when empty: load head, fail-fast
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

namespace crucible::concurrent {

// ── SpscValue<T> concept ──────────────────────────────────────────
//
// Constraints on T for safe storage in a non-atomic cell that's
// guarded only by the producer/consumer sync on head/tail.

template <typename T>
concept SpscValue =
    std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T>;

// ── SpscRing<T, Capacity> ─────────────────────────────────────────

template <SpscValue T, std::size_t Capacity>
class SpscRing : public safety::Pinned<SpscRing<T, Capacity>> {
public:
    using value_type = T;
    static constexpr std::size_t channel_capacity = Capacity;

    static_assert(std::has_single_bit(Capacity),
                  "Capacity must be a power of two");
    static_assert(Capacity > 0,
                  "Capacity must be greater than zero");

private:
    static constexpr std::uint64_t MASK = std::uint64_t{Capacity - 1};

public:
    SpscRing() noexcept = default;

    // ── try_push (sole producer) ──────────────────────────────────
    //
    // Memory ordering:
    //   - load head_ relaxed (own variable, no cross-thread sync)
    //   - load tail_ acquire (cross-thread; sync with consumer's
    //     release on tail to know slots are free for reuse)
    //   - cell write (non-atomic; producer's exclusive slot)
    //   - store head_ release (publishes the cell write to consumer's
    //     acquire on head)
    //
    // SPSC slot non-overlap proof: producer writes buffer_[h & MASK]
    // only after verifying h - t < Capacity, which means the slot
    // (h & MASK) was previously read by the consumer at tail position
    // (h - Capacity), i.e., the consumer has already moved past it.
    // The slot indexed by (t & MASK) is NEVER the same as (h & MASK)
    // when both are in flight, so no concurrent slot access occurs.
    [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
        // peek_relaxed: producer reads its own head — no cross-thread sync.
        const std::uint64_t h = head_.peek_relaxed();
        // get(): acquire on tail — pair with consumer's release in advance().
        const std::uint64_t t = tail_.get();
        if (h - t >= Capacity) [[unlikely]] {
            return false;  // full
        }
        buffer_[h & MASK] = item;
        // advance: release-store with monotonicity contract; under hot-TU
        // contract semantics the pre collapses to [[assume]] and emits zero
        // codegen — same store(release) as the prior bare atomic.
        head_.advance(h + 1);
        return true;
    }

    // ── try_pop (sole consumer) ───────────────────────────────────
    //
    // Memory ordering:
    //   - load tail_ relaxed (own variable, no cross-thread sync)
    //   - load head_ acquire (cross-thread; sync with producer's
    //     release on head to see the cell write)
    //   - cell read (non-atomic; consumer's exclusive slot since
    //     it's the sole reader of [tail, head))
    //   - store tail_ release (publishes "slot free" to producer's
    //     acquire on tail)
    [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
        // peek_relaxed: consumer reads its own tail — no cross-thread sync.
        const std::uint64_t t = tail_.peek_relaxed();
        // get(): acquire on head — pair with producer's release in advance().
        const std::uint64_t h = head_.get();
        if (h == t) [[unlikely]] {
            return std::nullopt;  // empty
        }
        const T result = buffer_[t & MASK];
        // advance: release-store with monotonicity contract.
        tail_.advance(t + 1);
        return result;
    }

    // ── try_push_batch (sole producer) ────────────────────────────
    //
    // Push up to `items.size()` items in one shot.  Returns the number
    // actually pushed (clamped to ring's available capacity).  Per-item
    // amortized cost: 2 atomic loads + (count) stores + 1 atomic store,
    // divided by count.  At count=64 measured per-item cost on Zen 3
    // is ~0.12 ns — bandwidth-bound, not latency-bound.
    //
    // Memcpy is the underlying primitive — the compiler intrinsic
    // vectorizes contiguous copies to whatever wide-store ISA the
    // target supports (-march=native): AVX2 256-bit YMM stores on
    // Zen 3 / Skylake, AVX-512 512-bit ZMM on Zen 4+ / Ice Lake+,
    // SSE2 128-bit XMM as the portable floor.  We do NOT call
    // std::simd here — std::memcpy with a known-bounded contiguous
    // range produces equivalent or better codegen via the compiler's
    // memcpy intrinsic, with no header dependency on <simd>.
    //
    // The wrap-around case (start_pos + count > Capacity) splits into
    // two contiguous memcpys.
    //
    // SPSC contract preserved: producer is sole writer of head_ and
    // buffer_[tail .. head); the count atomic claim publishes all
    // writes via head_.advance's release-store.
    [[nodiscard, gnu::hot]] std::size_t try_push_batch(
        std::span<const T> items) noexcept {
        if (items.empty()) [[unlikely]] return 0;
        const std::uint64_t h = head_.peek_relaxed();
        const std::uint64_t t = tail_.get();
        const std::uint64_t free_slots = Capacity - (h - t);
        if (free_slots == 0) [[unlikely]] return 0;
        const std::size_t count = std::min<std::size_t>(items.size(), free_slots);

        // Wrap split: at most two contiguous runs.
        const std::uint64_t start_pos = h & MASK;
        const std::size_t first = std::min<std::size_t>(
            count, Capacity - start_pos);
        const std::size_t second = count - first;

        // Bulk writes — compiler emits AVX-512/AVX2/SSE memcpy as available.
        std::memcpy(buffer_.data() + start_pos, items.data(),
                    first * sizeof(T));
        if (second > 0) [[unlikely]] {
            std::memcpy(buffer_.data(), items.data() + first,
                        second * sizeof(T));
        }

        head_.advance(h + count);
        return count;
    }

    // ── try_pop_batch (sole consumer) ─────────────────────────────
    //
    // Pop up to `out.size()` items; returns the number actually popped.
    // Per-item amortized cost mirrors try_push_batch.  Caller's `out`
    // span must accommodate the return — partial fills allowed.
    [[nodiscard, gnu::hot]] std::size_t try_pop_batch(
        std::span<T> out) noexcept {
        if (out.empty()) [[unlikely]] return 0;
        const std::uint64_t t = tail_.peek_relaxed();
        const std::uint64_t h = head_.get();
        const std::uint64_t available = h - t;
        if (available == 0) [[unlikely]] return 0;
        const std::size_t count = std::min<std::size_t>(out.size(), available);

        const std::uint64_t start_pos = t & MASK;
        const std::size_t first = std::min<std::size_t>(
            count, Capacity - start_pos);
        const std::size_t second = count - first;

        std::memcpy(out.data(), buffer_.data() + start_pos,
                    first * sizeof(T));
        if (second > 0) [[unlikely]] {
            std::memcpy(out.data() + first, buffer_.data(),
                        second * sizeof(T));
        }

        tail_.advance(t + count);
        return count;
    }

    // ── empty_approx / size_approx (any thread, NOT exact) ────────
    //
    // Snapshot reads — values may change immediately after return.
    // Use for telemetry / "should we keep polling?" decisions only,
    // NEVER for correctness invariants.

    [[nodiscard]] bool empty_approx() const noexcept {
        return head_.get() == tail_.get();
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        // size_t == uint64_t on our supported platforms (x86-64,
        // aarch64); the implicit conversion is exact.  An explicit
        // cast would trigger -Werror=useless-cast.
        const std::uint64_t h = head_.get();
        const std::uint64_t t = tail_.get();
        return h - t;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    // Cross-thread atomics on isolated cache lines — head and tail
    // on one shared line would ping-pong MESI on every push/pop.
    //
    // AtomicMonotonic surfaces the SPSC publish discipline at the type
    // level: peek_relaxed for own-side reads, advance for release-store
    // with monotonicity contract, get for cross-thread acquire.  Hot-
    // path codegen identical to the prior raw atomic.

    alignas(64) safety::AtomicMonotonic<std::uint64_t> head_{0};
    alignas(64) safety::AtomicMonotonic<std::uint64_t> tail_{0};

    // Buffer of T values.  Producer writes [tail, head), consumer
    // reads [tail, head); the SPSC contract makes per-slot races
    // impossible.
    alignas(64) std::array<T, Capacity> buffer_{};
};

}  // namespace crucible::concurrent
