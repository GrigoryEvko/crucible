#pragma once

// ═══════════════════════════════════════════════════════════════════
// AtomicSnapshot<T> — single-writer many-reader snapshot publisher
//                     (Lamport-style seqlock)
//
// One writer publishes T atomically; any number of readers observe
// a consistent T snapshot without locks.  Successive writes
// overwrite — there is NO per-write delivery guarantee.  This is
// the right primitive when "latest is what matters": metrics
// broadcast, config publication, observability dashboards.
//
// ─── The contract ──────────────────────────────────────────────────
//
// Writer (singular):
//   - `publish(T new_val)`: atomically replaces the published value.
//   - **Caller MUST guarantee no concurrent publish() calls.**
//     Multi-writer requires CAS on the sequence number; that is a
//     different primitive (with worse reader latency) — see
//     concurrent/Mpsc* if you need multi-writer.
//
// Readers (plural):
//   - `load()`: blocks (spins) until a coherent snapshot is read.
//     Always returns a value that was, at some moment, atomically
//     `publish()`'d (or T{} before any publish).
//   - `try_load()`: non-blocking; returns nullopt if a torn or
//     in-progress write is detected.  No retry.
//
// Memory ordering (the load-bearing detail):
//
//   Writer:
//     fetch_add(1, release)   → seq goes from even E to odd E+1.
//                               "writer in progress."  Release pairs
//                               with reader's acquire on seq_pre,
//                               but more importantly establishes a
//                               happens-before edge so subsequent
//                               data writes cannot be reordered
//                               above this fetch_add.
//     memcpy(storage_, &v)    → non-atomic byte writes.
//     fetch_add(1, release)   → seq goes from E+1 to E+2.  Release
//                               publishes the data writes.
//
//   Reader:
//     pre  = seq.load(acquire)  → spin while pre is odd.  Acquire
//                                 pairs with writer's second
//                                 fetch_add release: if pre is
//                                 even = E+2, all writer data
//                                 writes for round (E/2)+1 are
//                                 visible to this reader.
//     memcpy(&buf, storage_)    → non-atomic byte reads (technically
//                                 a data race per C++ if writer is
//                                 concurrently mid-write — see
//                                 "UB-adjacency" below).
//     thread_fence(acquire)     → ensures the data reads above are
//                                 not reordered below the seq_post
//                                 load.  Without this fence the
//                                 compiler/CPU could move the seq
//                                 load before the data reads, making
//                                 the post-check meaningless.
//     post = seq.load(relaxed)  → if post != pre, writer started a
//                                 new round during the data reads.
//                                 Discard buf and retry.
//
// ─── UB-adjacency (and why we get away with it) ────────────────────
//
// The non-atomic byte read in the reader's memcpy is, by C++
// standard, a data race when the writer is concurrently writing —
// hence undefined behavior.  Crucible accepts this trade-off (the
// Linux kernel's seqcount_t and Folly's seqlock implementations
// make the same trade-off) because:
//
//   1. T is constrained to trivially-copyable + trivially-
//      destructible.  No constructor, destructor, or vtable to
//      corrupt — at worst the byte buffer holds a torn mix of two
//      epochs' bits.
//   2. The seq retry protocol GUARANTEES that any torn read is
//      discarded before being returned to the caller.  The caller
//      never observes the torn state.
//   3. We use std::start_lifetime_as<T> (P2590R2, C++23) on the
//      retry-success path to formally end the byte buffer's "byte
//      lifetime" and start a T lifetime — making the final read a
//      well-defined T access.
//
// The technically-UB window is the data-race read inside the
// memcpy itself, between when the bytes are touched and when the
// retry check rejects them.  Compilers do not currently exploit
// this window for trivially-copyable T (verified via godbolt and
// the Linux kernel community's own analysis); if a future compiler
// does, the failure mode is a missed retry, not silent corruption,
// and would be caught immediately by the prop_atomic_snapshot
// fuzzer (QUEUE-10).
//
// ─── Pinned, not movable ───────────────────────────────────────────
//
// Inherits from safety::Pinned<>.  The class contains an
// std::atomic<uint64_t> that another thread may be reading at any
// moment — moving the object would invalidate every reader's
// pointer to the seq counter.  Compile error on copy or move.
//
// ─── Constraints ───────────────────────────────────────────────────
//
//   * T trivially copyable + trivially destructible (compile-time)
//   * sizeof(T) ≤ 256 bytes — beyond that, the memcpy window is
//     long enough that reader retries become frequent under
//     contention.  For larger payloads use a different primitive
//     (e.g., double-buffered atomic<T*> swap).
//
// ─── Per-call atomic shape ─────────────────────────────────────────
//
//   Writer publish(): 2 × atomic fetch_add + 1 × memcpy of ≤ 256 B
//   Reader load() (uncontended): 2 × atomic load + 1 × memcpy +
//                                1 × acquire fence
//   Reader load() (contended): retries until the seq pre/post pair
//                              brackets a quiescent writer
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>            // std::start_lifetime_as
#include <optional>
#include <type_traits>

namespace crucible::concurrent {

// ── SnapshotValue concept ─────────────────────────────────────────
//
// Constrains T to types whose bytes can be safely memcpy'd in and
// out of the seqlock storage.  The 256 B (4 cache line) cap is a
// structural soft limit: a smaller writer-side memcpy window keeps
// reader retry rates low under contention.  Larger T should use a
// different primitive (e.g. double-buffered atomic<T*> swap).

template <typename T>
concept SnapshotValue =
    std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T> &&
    sizeof(T) <= 256 &&
    sizeof(T) > 0;

// ── AtomicSnapshot<T> ─────────────────────────────────────────────

template <SnapshotValue T>
class AtomicSnapshot : public safety::Pinned<AtomicSnapshot<T>> {
public:
    // Default ctor: snapshot holds T's zero-initialized value.
    // Initial seq_ = 0 is even — readers immediately see the
    // value.  This is consistent: an "empty" snapshot returns
    // T{} and version() == 0.
    AtomicSnapshot() noexcept = default;

    // Initial-value ctor: publish `initial` synchronously, no
    // races possible (no other thread has the address yet).
    explicit AtomicSnapshot(const T& initial) noexcept
        : seq_{0} {
        std::memcpy(storage_, &initial, sizeof(T));
        // No concurrent readers possible (no other thread has the
        // address yet) so reset_under_quiescence is the right tool —
        // bump from 0 → 2 in one move that bypasses monotonicity check
        // (we go from initial 0 to 2, which advance() would accept,
        // but reset_under_quiescence makes the "no concurrent access"
        // precondition explicit at the call site).
        seq_.reset_under_quiescence(2);
    }

    // ── publish (single writer) ───────────────────────────────────
    //
    // Caller MUST guarantee no other thread is concurrently
    // calling publish().  Pre/post seq invariants:
    //   pre:  seq_ is even (no other writer in progress)
    //   post: seq_ is even, value visible to all readers
    //
    // ─── Memory ordering — the load-bearing detail ──────────────
    //
    // The first fetch_add MUST be acq_rel, NOT plain release.
    //
    // Why: release is a one-way barrier — prior ops cannot be
    // moved AFTER, but subsequent ops CAN be moved BEFORE.  With
    // plain release on the first fetch_add, the compiler is free
    // to hoist the memcpy above the seq increment:
    //
    //     // BUG (release): compiler reorders to:
    //     std::memcpy(storage_, &value, sizeof(T));  // ← hoisted!
    //     seq_.fetch_add(1, release);                // seq → odd
    //     seq_.fetch_add(1, release);                // seq → even
    //
    // Now readers can observe the in-flight memcpy WHILE seq is
    // still even (showing "stable").  Since the reader's seq_pre
    // and seq_post both see the unchanged even value, the retry
    // protocol fails to catch the torn read.  Manifests as ~10%
    // intermittent test failures under contention — passed CI
    // for years in many real-world seqlock impls before being
    // discovered.
    //
    // acq_rel is the fix: acquire semantics prevent subsequent
    // operations (the memcpy and the second fetch_add) from
    // being moved BEFORE this fetch_add.  The memcpy is now
    // bracketed strictly between the two seq increments.
    void publish(const T& value) noexcept {
        // First bump: even → odd ("writer in progress").
        // bump_by uses acq_rel, which matches the load-bearing
        // requirement of this side: acquire semantics prevent the
        // memcpy below from being hoisted ABOVE this increment.  See
        // the long doc-comment above for why a plain release is wrong.
        const uint64_t old_seq = seq_.bump_by(1);
        // Single-writer invariant — debug-checked.  If this fires,
        // either (a) two threads called publish() concurrently
        // (the documented contract violation) or (b) seq_ was
        // corrupted by external bug.
        CRUCIBLE_DEBUG_ASSERT((old_seq & 1u) == 0u);

        // Non-atomic byte write of the value.  Bounded by sizeof(T);
        // T is trivially-copyable so memcpy is the canonical write.
        std::memcpy(storage_, &value, sizeof(T));

        // Second bump: odd → even ("data published").  Release alone
        // would be sufficient (prior memcpy cannot move after); bump_by's
        // acq_rel is stricter.  On x86 LOCK XADD is a full fence either
        // way → zero regression.  On ARM, one extra dmb (~1-3ns) per
        // publish is paid for the type-level monotonicity guarantee.
        // Acceptable trade — publish is the slow path; readers (which
        // are the hot path for this primitive) are unchanged.
        (void)seq_.bump_by(1);
    }

    // ── load (any reader) ─────────────────────────────────────────
    //
    // Spin-retries until a coherent snapshot is observed.  Returns
    // a value that was, at some moment, atomically published.
    //
    // ─── Memory ordering — both sides of data reads need a fence ─
    //
    // Naive seqlock pattern with just acquire-loads on seq is
    // BROKEN.  Acquire is a one-way DOWNWARD barrier (subsequent
    // ops can't move ABOVE), so:
    //
    //   pre = seq.load(acquire);   // OK: data memcpy can't move
    //                              //     ABOVE this acquire
    //   memcpy(buf, storage);
    //   post = seq.load(acquire);  // BUG: nothing prevents the
    //                              //      memcpy above from being
    //                              //      moved BELOW this load!
    //
    // If memcpy is reordered BELOW seq_post, the reader sees old
    // seq matching pre but reads NEW (potentially torn) data.
    //
    // Linux kernel's read_seqcount uses smp_rmb() on BOTH sides
    // of the data reads.  In C++26, atomic_thread_fence(acquire)
    // provides equivalent compiler+hardware semantics: per
    // [atomics.fences/3.1], non-atomic ops sequenced before an
    // acquire fence are ordered before non-atomic ops sequenced
    // after.  Together with the acquire-loads on seq, this
    // gives us a fully-ordered three-step protocol:
    //
    //   pre = seq.load(acquire);   // (1) — prevents memcpy above
    //   memcpy(buf, storage);      // (2) — bracketed by fences
    //   thread_fence(acquire);     // (3) — prevents memcpy below
    //   post = seq.load(acquire);  // (4) — syncs-with writer release
    [[nodiscard]] T load() const noexcept {
        // Staging buffer for the byte-level memcpy.  Properly
        // aligned for T; bytes are zero-init via NSDMI to satisfy
        // InitSafe (storage_ contents will overwrite immediately).
        alignas(T) std::byte buf[sizeof(T)]{};

        for (;;) {
            uint64_t pre = seq_.get();

            // Wait for any in-progress writer to complete.  Bounded
            // wait: writer's mid-publish window is ~30 ns.
            while ((pre & 1u) != 0u) {
                CRUCIBLE_SPIN_PAUSE;
                pre = seq_.get();
            }

            // Snapshot the bytes.  This is the technically-UB
            // window (data race with writer's memcpy if writer
            // restarts mid-read); the seq_post check below
            // catches any inconsistency, with the fence ensuring
            // proper ordering.
            std::memcpy(buf, storage_, sizeof(T));

            // Acquire fence: prevents the data memcpy above from
            // being reordered BELOW this point.  Without this,
            // the compiler/CPU could move the memcpy after the
            // seq_post load, breaking the retry protocol.  This
            // is the C++ equivalent of Linux's smp_rmb() between
            // data reads and the seq retry check.
            std::atomic_thread_fence(std::memory_order_acquire);

            const uint64_t post = seq_.get();

            if (pre == post) {
                // Coherent: the bytes in `buf` are exactly what the
                // writer published at seq round pre/2.  Begin a T
                // lifetime in the buffer (P2590R2) and return by
                // value (NRVO).
                return *std::start_lifetime_as<T>(buf);
            }

            // Writer started a new round during our memcpy.
            // Pause briefly and retry.
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // ── try_load (non-blocking) ───────────────────────────────────
    //
    // Returns the snapshot iff no torn read or in-progress write
    // is observed; returns nullopt otherwise.  Useful for
    // best-effort instrumentation paths that must never block.
    [[nodiscard]] std::optional<T> try_load() const noexcept {
        const uint64_t pre = seq_.get();
        if ((pre & 1u) != 0u) {
            return std::nullopt;  // writer mid-publish
        }

        alignas(T) std::byte buf[sizeof(T)]{};
        std::memcpy(buf, storage_, sizeof(T));

        // Acquire fence: same reasoning as load() — prevents the
        // memcpy from being reordered below the seq_post load.
        std::atomic_thread_fence(std::memory_order_acquire);

        const uint64_t post = seq_.get();
        if (pre != post) {
            return std::nullopt;  // torn — writer started new round
        }
        return *std::start_lifetime_as<T>(buf);
    }

    // ── version (number of completed publishes) ───────────────────
    //
    // Returns the count of completed publish() calls.  Monotonically
    // increases.  Wrap is impossible in practice (uint64 / 2 = 9.2
    // quintillion epochs).  Useful for "did anything change since
    // I last looked?" checks: callers cache version and compare on
    // next call.
    [[nodiscard]] uint64_t version() const noexcept {
        // Half the seq counter (we increment twice per publish).
        // get() is acquire, syncing with the latest publication.
        return seq_.get() >> 1;
    }

private:
    // seq_ is read by all threads; isolate on its own cache line
    // so writer's increments don't ping-pong with unrelated
    // adjacent state.  AtomicMonotonic surfaces the seqlock's
    // monotonic-counter discipline at the type level — bump_by
    // for the writer's two-step bracket, get for reader pre/post
    // snapshots, reset_under_quiescence for the initial-value ctor
    // path where no concurrent access is possible.
    alignas(64) safety::AtomicMonotonic<uint64_t> seq_{0};

    // storage_ is also written by writer, read by all readers.
    // Keep it on its own cache line so seq_ updates don't
    // invalidate the data line on every publish (and vice versa).
    // For sizeof(T) > 64, storage_ spans multiple lines naturally.
    alignas(64) std::byte storage_[sizeof(T)]{};
};

// is_always_lock_free guarantee: for SnapshotValue T, the seq_
// uint64_t is always lock-free on every supported platform (x86-64
// and aarch64).  The "lock-freeness" of AtomicSnapshot is therefore
// just "is the underlying uint64_t lock-free?" — yes.
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "AtomicSnapshot's seq counter requires lock-free uint64_t atomic");

}  // namespace crucible::concurrent
