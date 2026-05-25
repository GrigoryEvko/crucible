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
// ─── Two distinct UB concerns; two distinct mitigations ────────────
//
// FIXY-FOUND-110/112 honest framing: the seqlock construction has
// TWO separate undefined-behavior concerns.  Conflating them
// (claiming a single mechanism handles both) is incorrect; each
// has its own mitigation and its own honest limitation.
//
// ── UB concern #1: the data-race during memcpy ─────────────────────
//
// The non-atomic byte read in the reader's memcpy is, by C++
// standard, a data race when the writer is concurrently writing.
// `std::start_lifetime_as<T>` (P2590R2) **does NOT launder this** —
// the paper handles the type-system aspect only (byte→T lifetime
// transition; see UB concern #2 below).  It cannot fix a data race;
// the race is in the bytes' READS, prior to any type interpretation.
//
// Mitigations for concern #1 (matched to Linux kernel seqcount_t
// and Folly's seqlock, both of which accept the same trade-off):
//
//   1. T is constrained to trivially-copyable + trivially-
//      destructible.  No constructor, destructor, or vtable to
//      corrupt — at worst the byte buffer holds a torn mix of two
//      epochs' bits.  No silent invariant-violation can be
//      synthesized from a torn byte read of a trivial T.
//   2. The seq retry protocol GUARANTEES that any torn read is
//      discarded before being returned to the caller.  The caller
//      never observes the torn bytes.
//   3. Compilers do not currently exploit this data-race window for
//      trivially-copyable T (verified via godbolt and the Linux
//      kernel community's analysis).  The technically-UB window is
//      between when the racy bytes are touched and when the retry
//      check rejects them; compilers treat the memcpy as opaque
//      bytes and do not speculate through it.
//   4. The prop_atomic_snapshot fuzzer (QUEUE-10) treats this as the
//      load-bearing safety witness — if a future compiler starts
//      exploiting the window, the fuzzer catches the missed retry
//      under stress (the failure mode is a missed retry, not
//      silent corruption).
//
// ── UB concern #2: the byte-buffer-to-T lifetime transition ────────
//
// After the seqlock retry confirms the bytes are coherent, the
// caller needs to ACCESS those bytes as a T.  Without an explicit
// lifetime-start, reading a T-shaped value from a `std::byte` buffer
// is UB per [basic.life] — the bytes have byte lifetime, not T
// lifetime.  This is a TYPE-SYSTEM concern, ORTHOGONAL to the
// data-race above.
//
// Mitigation for concern #2:
//
//   * std::start_lifetime_as<T> (P2590R2, C++23) on the retry-
//     success path formally ends the byte buffer's byte lifetime
//     and starts a T lifetime in-place.  The subsequent `*` deref
//     is a well-defined T access.  This is THE feature P2590R2
//     was designed to provide; it does exactly what the standard
//     advertises and nothing more.
//
// ── Why the distinction matters ────────────────────────────────────
//
// A previous version of this doc claimed start_lifetime_as "made
// the final read a well-defined T access" in a way that implied
// it handled the data race.  Two reviewers in sequence read this
// as "P2590R2 launders seqlock races" — it does not.  Honest
// separation: data-race mitigation is the four items above (T
// constraint + retry + compiler-trust + fuzzer); type-system
// lifetime is the single P2590R2 call at retry-success.  Both
// are necessary; neither subsumes the other.
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
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Wait.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>           // std::abort (FIXY-FOUND-011 release-time check)
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
    using value_type = T;

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
        // ── FIXY-FOUND-011: ALWAYS-ON single-writer contract check ────
        //
        // Pre-FOUND-011 (2026-05-25) this was a CRUCIBLE_DEBUG_ASSERT
        // that stripped to `((void)0)` under -DNDEBUG.  That left the
        // production NDEBUG path unguarded against the single-writer
        // contract violation, with two concrete failure modes:
        //
        //   (1) TORN READ — two concurrent publishers' memcpys race;
        //       a reader observing the second publisher's seq=even
        //       sees a byte-mix of both writers' data.
        //
        //   (2) PERMANENT ODD-SEQ PARK — a publisher T1 that bumps
        //       once (seq even→odd) then dies/cancels before its
        //       second bump leaves seq at an odd value.  Subsequent
        //       publishers' even-numbered bump pairs can interleave
        //       to land seq at odd indefinitely.  Readers spinning
        //       on `(pre & 1u) != 0u` (line ~322) spin forever.
        //
        // The check is now an always-on branch (1 bit-test + 1
        // conditional branch under [[unlikely]], ~1 ns slow-path
        // cost; readers unchanged).  std::abort() is the correct
        // response — a publisher contract violation is a CALLER bug
        // and continuing executes undefined state (the next publish()
        // could leave seq permanently odd, parking all readers).
        // CRUCIBLE_ASSERT was considered but rejected: under
        // contract-evaluation-semantic=ignore (hot-path TUs) it
        // collapses to nothing, reintroducing the FOUND-011 silent-
        // violation surface.
        if ((old_seq & 1u) != 0u) [[unlikely]] {
            std::abort();
        }

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
                // writer published at seq round pre/2 (the data-
                // race-mitigation chain in the header doc-block
                // applies: trivial-T + retry + compiler-trust +
                // fuzzer).  Begin a T lifetime in the buffer via
                // P2590R2 — this is the type-system step ONLY; it
                // does NOT relate to the data-race mitigation.
                // See FIXY-FOUND-112 in the header doc-block for
                // the explicit separation of the two concerns.
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
        // Coherent; same FIXY-FOUND-112 framing as load(): the
        // P2590R2 call handles the byte→T lifetime transition only.
        // Data-race mitigation lives in the trivial-T constraint +
        // retry protocol + compiler-trust + fuzzer chain documented
        // in the header.
        return *std::start_lifetime_as<T>(buf);
    }

    // ═══════════════════════════════════════════════════════════════
    // FOUND-G27: Wait-pinned production surface
    // ═══════════════════════════════════════════════════════════════
    //
    // load() spins on the seqlock with CRUCIBLE_SPIN_PAUSE while the
    // writer is mid-publish (~30 ns expected window).  The wait
    // strategy IS SpinPause — top of the WaitLattice (Block ⊑ Park
    // ⊑ AcquireWait ⊑ UmwaitC01 ⊑ BoundedSpin ⊑ SpinPause).
    //
    // load_pinned() pins that classification at the type level:
    // hot-path consumers declaring `requires Wait::satisfies<
    // SpinPause>` admit the value; consumers expecting Park or
    // weaker tiers reject it (would be a bug — they should use a
    // Park-strategy primitive instead).
    //
    // Why additive (not replacing): existing call sites of load()
    // (runtime metrics broadcast, the snapshot consumer pattern in
    // the audit harness) consume bare T.  An additive overlay
    // preserves those sites while letting NEW production consumers
    // declare their wait-strategy constraint at the type level.
    //
    // Cost: zero overhead beyond load().  Wait<SpinPause, T> is EBO-
    // collapsed; the constructor is a single move; the same memcpy/
    // start_lifetime_as machinery runs.

    // Hot-path-classified load — returns Wait<SpinPause, T>.
    [[nodiscard]] safety::Wait<safety::WaitStrategy_v::SpinPause, T>
    load_pinned() const noexcept {
        return safety::Wait<safety::WaitStrategy_v::SpinPause, T>{load()};
    }

    // ═══════════════════════════════════════════════════════════════
    // FOUND-G32: MemOrder-pinned production surface
    // ═══════════════════════════════════════════════════════════════
    //
    // load() / try_load() / version() all use Acquire ordering on the
    // seq counter and an Acquire fence between the data memcpy and
    // the seq_post comparison (see "Memory ordering — both sides of
    // data reads need a fence" docblock above).  The OPERATION'S
    // memory-order classification is Acquire — the strongest claim
    // the reader makes about the synchronization with the writer's
    // Release publication.
    //
    // load_mo_pinned() / try_load_mo_pinned() / version_mo_pinned()
    // pin that classification at the type level.  Hot-path consumers
    // that declare `requires MemOrder::satisfies<Acquire>` admit
    // these values (Acquire trivially satisfies Acquire); consumers
    // expecting Release-or-stronger reject them (the reader side
    // legitimately is Acquire, so a Release-fence-requiring
    // consumer is in the wrong slot of the producer/consumer
    // pairing).
    //
    // Lattice direction (MemOrderLattice.h):
    //   SeqCst(weakest) ⊑ AcqRel ⊑ Release ⊑ Acquire ⊑ Relaxed(strongest)
    //
    // satisfies<Required> = leq(Required, Self).  Stronger hardware-
    // friendliness subsumes weaker required-friendliness.  The load-
    // bearing rejection: a SeqCst-emitting site (added during a
    // refactor to make a bug go away) cannot reach a hot-path
    // consumer requiring Acquire — leq(Acquire, SeqCst) = false.
    //
    // Why additive: existing load() callers (runtime metrics broadcast,
    // SnapshotValue tests) consume bare T.  The pinned overlay is
    // for NEW production consumers that want the SeqCst-creep rejection
    // at the consumer fence.

    // Acquire-classified load — returns MemOrder<Acquire, T>.
    [[nodiscard]] safety::MemOrder<safety::MemOrderTag_v::Acquire, T>
    load_mo_pinned() const noexcept {
        return safety::MemOrder<safety::MemOrderTag_v::Acquire, T>{load()};
    }

    // Acquire-classified try_load — returns optional<MemOrder<Acquire, T>>.
    [[nodiscard]] std::optional<
        safety::MemOrder<safety::MemOrderTag_v::Acquire, T>>
    try_load_mo_pinned() const noexcept {
        auto opt = try_load();
        if (!opt) return std::nullopt;
        return safety::MemOrder<safety::MemOrderTag_v::Acquire, T>{
            std::move(*opt)};
    }

    // Acquire-classified version — returns MemOrder<Acquire, uint64_t>.
    // The seq_.get() is acquire-load on the seqlock counter; the
    // returned epoch number is an Acquire-tier observation.
    [[nodiscard]] safety::MemOrder<safety::MemOrderTag_v::Acquire, uint64_t>
    version_mo_pinned() const noexcept {
        return safety::MemOrder<safety::MemOrderTag_v::Acquire, uint64_t>{
            version()};
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
