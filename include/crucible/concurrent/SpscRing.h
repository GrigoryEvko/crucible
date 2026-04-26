#pragma once

// ═══════════════════════════════════════════════════════════════════
// SpscRing<T, Capacity> — single-producer single-consumer bounded ring
//
// The simplest concurrent primitive: one producer pushes, one
// consumer pops, lock-free, bounded.  Per-op cost ~5-8 ns
// (uncontended) — no CAS, just acquire/release atomic loads on
// head/tail and one cache-line-aligned cross-thread sync per call.
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
//                         sequence number protocol; ~12-15 ns per
//                         producer push (CAS on global head).  Use
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
// ─── Performance targets ────────────────────────────────────────────
//
//   try_push (uncontended): ~5-8 ns (1 acquire-load on tail,
//                                    1 cell store, 1 release-store)
//   try_pop  (uncontended): ~5-8 ns (1 acquire-load on head,
//                                    1 cell load, 1 release-store)
//   try_push when full:     ~3 ns (load tail, fail-fast)
//   try_pop  when empty:    ~3 ns (load head, fail-fast)
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
