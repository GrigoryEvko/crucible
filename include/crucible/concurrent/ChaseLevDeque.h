#pragma once

// ═══════════════════════════════════════════════════════════════════
// ChaseLevDeque<T, Capacity> — owner-friendly work-stealing deque
//
// Single-owner, multi-thief lock-free deque.  The owner pushes and
// pops at the BOTTOM (LIFO — best cache locality, no atomic CAS on
// the common path).  Thieves steal from the TOP (FIFO — only thieves
// contend with each other, never the owner on the fast path).
//
// Original algorithm: Chase & Lev, "Dynamic Circular Work-Stealing
// Deque" (SPAA '05).  Memory-order analysis: Lê, Pop, Cohen, &
// Nardelli, "Correct and Efficient Work-Stealing for Weak Memory
// Models" (PPoPP '13) — they proved EXACTLY which orderings are
// needed under C++11; this implementation follows their formulation.
//
// ─── Why Chase-Lev over central MPMC ───────────────────────────────
//
// Per-worker deque (this primitive) means the owner pays NO
// contention cost on push/pop — the bottom side is a single-thread
// load/store with one acquire load on `top_` for the capacity
// check.  Thieves only contend when they're stealing from the SAME
// deque (cross-worker stealing is the conflict path).
//
// Standard design for fork-join thread pools: TBB, Go runtime,
// Java ForkJoin, Cilk.  Crucible uses it for the kernel compile
// pool (QUEUE-5): main keeper pushes compile jobs to its own
// deque, worker threads steal jobs to compile.
//
// ─── The two seq_cst points (load-bearing) ─────────────────────────
//
// Owner pop_bottom and thief steal_top can race on the LAST
// element.  Without sequential consistency at the right two
// points, store-buffer reordering on x86-TSO (or weaker on ARM)
// lets BOTH sides observe the pre-decrement state and BOTH return
// the same item — duplication and item loss.
//
// The two seq_cst points (per Lê et al. 2013 §3.3):
//
//   1. After owner's bottom decrement in pop_bottom:
//        atomic_thread_fence(seq_cst);
//      Ensures the bottom store globally orders BEFORE the
//      subsequent top.load.  Thieves that have already loaded
//      top will be visible-CAS'd against by the owner's
//      compare_exchange_strong below.
//
//   2. Between thief's top.load and bottom.load in steal_top:
//        atomic_thread_fence(seq_cst);
//      Ensures the bottom load globally observes any owner's
//      bottom decrement that has happened.  If the fence were
//      omitted, the bottom.load could be reordered before the
//      top.load, missing the owner's claim.
//
// Both top CAS sites use seq_cst on success: the total order
// ensures only ONE side wins the race.
//
// ─── Constraints ───────────────────────────────────────────────────
//
//   * T must be trivially-copyable (we never destroy T on steal).
//   * std::atomic<T>::is_always_lock_free must hold.  In practice
//     this means sizeof(T) ≤ 16 bytes on x86-64 (cmpxchg16b) and
//     aarch64 (LSE casp).  For larger payloads, store T* and
//     allocate the body elsewhere.
//   * Capacity must be a power of two and > 0.
//
// ─── Per-call atomic shape ─────────────────────────────────────────
//
//   push_bottom: 1 relaxed load + 1 relaxed store on bottom_,
//                1 release store on the cell
//   pop_bottom:  1 relaxed load + 1 relaxed store on bottom_,
//                1 seq_cst fence, 1 acquire load on top_
//                (and a CAS on the last-element race)
//   steal_top:   1 acquire load on top_, 1 seq_cst fence,
//                1 acquire load on bottom_, 1 seq_cst CAS on top_
//   Steal contention scales with the number of thieves racing on
//   the same deque; the owner's bottom side stays uncontended.
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

// ── DequeValue<T> concept ─────────────────────────────────────────
//
// T must be safely storable in std::atomic<T> with always-lock-free
// guarantees.  This is enforced via:
//   * trivially-copyable: bytes-only semantics, no constructor
//     races on transfer
//   * trivially-destructible: stolen items leak no resources
//   * std::atomic<T>::is_always_lock_free: hardware atomic CAS
//     available without internal mutex

template <typename T>
concept DequeValue =
    std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T> &&
    std::atomic<T>::is_always_lock_free;

// ── ChaseLevDeque<T, Capacity> ────────────────────────────────────

template <DequeValue T, std::size_t Capacity>
class ChaseLevDeque : public safety::Pinned<ChaseLevDeque<T, Capacity>> {
public:
    using value_type = T;
    static constexpr std::size_t channel_capacity = Capacity;

    static_assert(std::has_single_bit(Capacity),
                  "Capacity must be a power of two");
    static_assert(Capacity > 0,
                  "Capacity must be greater than zero");
    static_assert(Capacity <= (std::size_t{1} << 30),
                  "Capacity must fit in 31-bit signed range "
                  "(top/bottom are int64; Capacity ≤ 2^30 keeps the "
                  "subtraction safe under any deque state)");

private:
    static constexpr int64_t MASK = static_cast<int64_t>(Capacity - 1);

public:
    ChaseLevDeque() noexcept = default;

    // ── push_bottom (owner only) ──────────────────────────────────
    //
    // Owner pushes to the bottom of the deque.  Returns false on
    // capacity overflow (caller's responsibility — bounded deque).
    //
    // Memory ordering:
    //   - load b: relaxed (own variable, no cross-thread sync)
    //   - load t: acquire (cross-thread; sync with thief's CAS-
    //             release on top so we observe steals up to t)
    //   - cell store: relaxed (atomic, but no ordering needed yet)
    //   - release fence: ensures cell store is visible BEFORE the
    //                    bottom store below propagates
    //   - bottom store: relaxed (release fence already sequenced)
    [[nodiscard]] bool push_bottom(T item) noexcept {
        const int64_t b = bottom_.load(std::memory_order_relaxed);
        // top_.get() is acquire — pair with thieves' CAS-release in
        // steal_top so we observe completed steals up to t.
        const int64_t t = top_.get();
        if (b - t >= static_cast<int64_t>(Capacity)) [[unlikely]] {
            return false;  // full
        }
        buffer_[b & MASK].store(item, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
        return true;
    }

    // ── pop_bottom (owner only) ───────────────────────────────────
    //
    // Owner pops from the bottom.  Returns nullopt if empty.
    //
    // The interesting case: when t == b after decrement, owner
    // and thief are racing for the LAST element.  CAS on top
    // resolves the race — if owner wins, takes the item; if
    // thief wins, owner restores bottom and returns empty.
    //
    // Memory ordering (Lê et al. 2013 §3.3, algorithm 5):
    //   - load b - 1: relaxed (own variable)
    //   - store b: relaxed (own variable, but visible to thieves
    //              only after the seq_cst fence below)
    //   - SEQ_CST FENCE: critical — globally orders the bottom
    //              decrement BEFORE the top load below.  Without
    //              this, store-buffer reordering on x86 lets the
    //              top load happen first, missing in-flight steals.
    //   - load t: relaxed (the seq_cst fence above has done the
    //             heavy lifting; top reads here see any thief's
    //             top updates that happened-before the fence)
    //   - cell load: relaxed (we own this slot until we hand it
    //               back to the deque)
    //   - CAS top: seq_cst on success (race resolution), relaxed
    //              on failure (no further write needed).
    [[nodiscard]] std::optional<T> pop_bottom() noexcept {
        int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);
        // The seq_cst fence here is THE Lê 2013 §3.3 critical
        // ordering point — orders the bottom decrement BEFORE the
        // top load.  Co-located with the AtomicMonotonic call
        // sites that bracket it.
        safety::AtomicMonotonic<int64_t>::fence_seq_cst();
        // top_.peek_relaxed() is correct here: the seq_cst fence
        // above provides the cross-thread ordering; the load itself
        // can be relaxed.
        int64_t t = top_.peek_relaxed();

        if (t > b) {
            // Empty: restore bottom and return.  The "+1" undoes
            // our decrement; we never had a valid slot to take.
            bottom_.store(b + 1, std::memory_order_relaxed);
            return std::nullopt;
        }

        const T item = buffer_[b & MASK].load(std::memory_order_relaxed);
        if (t < b) {
            // Strictly more than one element — no race possible.
            // Owner takes the bottom slot; thieves may still be
            // racing for top slots, but we own b.
            return item;
        }

        // t == b: exactly one element, race window with thieves.
        // CAS top: if it succeeds, we won the race (claimed t);
        // if it fails, a thief has incremented top to t+1 (= b+1)
        // and taken our item.  Either way, the deque is empty
        // after this call, so restore bottom.
        //
        // compare_exchange_advance enforces monotonicity at the
        // type level (pre: t < t+1).  Seq_cst on success acts as
        // the RMW fence Lê 2013 §3.3 requires.
        if (!top_.compare_exchange_advance(t, t + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed)) {
            // Thief got it.
            bottom_.store(b + 1, std::memory_order_relaxed);
            return std::nullopt;
        }
        bottom_.store(b + 1, std::memory_order_relaxed);
        return item;
    }

    // ── steal_top (any thread) ────────────────────────────────────
    //
    // Thief pops from the top.  Returns nullopt if empty OR if a
    // concurrent thief / owner won the CAS race.  Caller may
    // re-attempt; this primitive does NOT spin.
    //
    // Memory ordering (Lê et al. 2013 §3.3, algorithm 4):
    //   - load t: acquire (sync with previous top updates from
    //             other thieves' CAS-release; see all stolen slots)
    //   - SEQ_CST FENCE: critical — globally orders the top load
    //             BEFORE the bottom load below.  Without this, the
    //             bottom load could be reordered above the top
    //             load on weak memory models, missing an owner's
    //             pop_bottom decrement that has happened-after our
    //             top load.
    //   - load b: acquire (cross-thread; sync with owner's
    //             push_bottom release fence to see new items)
    //   - cell load: relaxed (the CAS below confirms whether we
    //               atomically claim the slot)
    //   - CAS top: seq_cst on success (race resolution), relaxed
    //              on failure.
    [[nodiscard]] std::optional<T> steal_top() noexcept {
        // top_.get() is acquire — sync with previous top updates from
        // other thieves' CAS-release; see all stolen slots.
        int64_t t = top_.get();
        safety::AtomicMonotonic<int64_t>::fence_seq_cst();
        const int64_t b = bottom_.load(std::memory_order_acquire);

        if (t >= b) {
            return std::nullopt;  // empty (or owner racing)
        }

        // Read the candidate item.  If our CAS below fails, this
        // value is discarded — another thief or the owner has
        // claimed slot t.
        const T item = buffer_[t & MASK].load(std::memory_order_relaxed);

        // compare_exchange_advance enforces monotonicity (pre: t < t+1).
        // Seq_cst on success — race-resolution against owner pop_bottom
        // and other thieves.  The pre() compiles to nothing under
        // hot-TU contract semantics; the runtime CAS is the same
        // libstdc++ compare_exchange_strong as before.
        if (!top_.compare_exchange_advance(t, t + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed)) {
            return std::nullopt;  // contention; caller may retry
        }

        return item;
    }

    // ── size_approx / empty_approx (any thread, NOT exact) ───────
    //
    // Snapshot reads — values may change immediately after return.
    // Useful for telemetry / "should we stop polling?" decisions,
    // NEVER for correctness invariants in caller logic.

    [[nodiscard]] std::size_t size_approx() const noexcept {
        const int64_t t = top_.get();          // acquire
        const int64_t b = bottom_.load(std::memory_order_acquire);
        const int64_t diff = b - t;
        return diff > 0 ? static_cast<std::size_t>(diff) : 0;
    }

    [[nodiscard]] bool empty_approx() const noexcept {
        return size_approx() == 0;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    // ── Storage layout ────────────────────────────────────────────
    //
    // All three members on their own cache lines:
    //   - top_:    contended between thieves; isolating prevents
    //              their CAS traffic from invalidating bottom_'s
    //              line.
    //   - bottom_: written only by owner; isolating prevents
    //              owner's stores from invalidating top_'s line.
    //   - buffer_: per-cell atomics; the array's first cells
    //              share a line with the last members of bottom_,
    //              so we align the buffer too to keep the cells
    //              cleanly separated from the counters.

    // top_ migrated to AtomicMonotonic<int64_t> per FOUND-A20.
    // The CAS on top_ in pop_bottom and steal_top routes through
    // compare_exchange_advance, which carries the monotonicity
    // contract (pre: new > observed) at the type level — making it
    // structurally impossible to accidentally CAS top_ backward.
    // Hot-path codegen identical to the prior bare atomic CAS.
    //
    // Pinned base — the original ChaseLevDeque already inherits
    // Pinned, and AtomicMonotonic is itself Pinned, so this is a
    // double-Pinned chain.  No double-base because Pinned is a
    // CRTP marker (zero data).
    alignas(64) safety::AtomicMonotonic<int64_t> top_{0};
    alignas(64) std::atomic<int64_t> bottom_{0};
    alignas(64) std::array<std::atomic<T>, Capacity> buffer_{};
};

}  // namespace crucible::concurrent
