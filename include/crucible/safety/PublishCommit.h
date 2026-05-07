#pragma once

// ── crucible::safety::PublishCommit<Tag, WriteAuth> ─────────────────
//
// Compile-time-typed harness around the foreground-visible commit
// counter of a multi-stage pipeline whose terminal stage is the only
// site where downstream observers ("fg sees mode_=COMPILED ⇒
// pending_region_ valid") may safely advance the counter.
//
// Background — the bug class this prevents:
//
//   GAPS-FLUSH-RACE (test_vigil_dispatch / test_end_to_end /
//   test_region_cache / test_vigil_deadline_watchdog).  The bg
//   pipeline has 4 stages: drain → detect → build → publish.  Stage 3
//   (build) historically bumped total_processed inline for commit-only
//   markers, BEFORE stage 4 (publish) had run on_region_ready.  fg's
//   flush() spins on total_processed; flush returning therefore
//   carried no synchronization on pending_region_ / mode_ /
//   active_region_ — they were stage-4 writes that hadn't happened
//   yet.  The result was a sub-microsecond window where mode_=COMPILED
//   ∧ pending_region_=null was observable; under load the four tests
//   above hit it every run.
//
//   The fix at the callsite was a one-liner: forward commit markers
//   through the channel to stage 4, and bump there.  But the bug class
//   is general: any "I have processed N items" counter that the
//   foreground polls AS A PROXY for "I have published the side
//   effects of those N items" is racy if the bump and the publish
//   live in different stages.
//
//   PublishCommit<Tag, WriteAuth> makes this structural.  The cell's
//   write surface (bump_by / bump) is gated by a friend list of ONE
//   type — `WriteAuth`.  Code outside `WriteAuth`'s body cannot call
//   bump.  Reads (load_acquire) are unrestricted; the foreground's
//   flush() peer can call them freely.
//
//   `WriteAuth` is the C++ type that physically performs the publish
//   action (e.g. a stage functor class, a member-function-holding
//   type, or a private nested type of the pipeline owner).  The
//   coupling between Tag and WriteAuth is part of the type — a
//   refactor that tries to bump from a different stage MUST either
//   (a) declare its own Tag/WriteAuth pair and have its own cell, or
//   (b) modify the original WriteAuth definition.  Both are visible
//   in code review.
//
// Wrapper algebra:
//
//   PublishCommitCell<Tag, WriteAuth>
//                              — owns std::atomic<uint64_t>; exposes
//                                unrestricted acquire-load reads
//                                (load_acquire, peek_relaxed, get) and
//                                a friend-gated write surface (bump_by,
//                                bump) callable only from inside
//                                WriteAuth's body.
//
//   The Tag is a phantom struct that prevents two different pipelines
//   in the same TU from accidentally sharing a cell type — even if
//   their WriteAuth happens to be the same class.  Always declare a
//   fresh Tag for each pipeline.
//
// Axiom coverage: BorrowSafe (single-stage writer, enforced by
// friend list), ThreadSafe (acquire/release pairing on the atomic),
// DetSafe (counter is monotonic; writes happen-after all preceding
// stage-4 publishes by source order), TypeSafe (Tag rejects cross-
// pipeline cell mismatches at compile time).
//
// Runtime cost: zero beyond the underlying std::atomic<uint64_t>.
// No witness object, no token to thread through call sites.  The
// gate is a friend declaration evaluated at template instantiation.
// ───────────────────────────────────────────────────────────────────

#include <crucible/Platform.h>

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ─── PublishCommitCell<Tag, WriteAuth> ──────────────────────────────
//
// Pinned, non-movable carrier of the post-publication counter.  Owns
// the std::atomic.  Reads are public; writes are friend-gated to
// `WriteAuth`.
//
// Tag is a phantom struct (declared at the use site, e.g.
// `struct BackgroundThreadPublishTag {};`).  Two distinct pipelines
// in the same translation unit MUST declare distinct Tags so their
// cells are distinct types — even if both happen to use the same
// WriteAuth.
template <typename Tag, typename WriteAuth>
class PublishCommitCell {
    alignas(64) std::atomic<uint64_t> value_{0};

    // Only WriteAuth (and its members) may call bump_by / bump.
    friend WriteAuth;

    // ─── Bump (write-side) — friend-only ───────────────────────────
    //
    // Located in the private section of the class so the only legal
    // callers are friends of the class.  WriteAuth is the sole
    // friend.  acq_rel ordering — release half pairs with
    // load_acquire().  Returns the previous counter value (issued-
    // ticket semantics — matches AtomicMonotonic::bump_by).
    //
    // No witness object: the friend list IS the witness.  Adding a
    // second authorized writer requires adding a second friend
    // declaration (and is therefore visible in review).
    uint64_t bump_by(uint64_t delta) noexcept {
        return value_.fetch_add(delta, std::memory_order_acq_rel);
    }

    uint64_t bump() noexcept {
        return bump_by(1);
    }

public:
    using tag_type        = Tag;
    using write_auth_type = WriteAuth;

    constexpr PublishCommitCell() noexcept = default;

    PublishCommitCell(const PublishCommitCell&)            = delete("PublishCommitCell owns the channel identity; not copyable");
    PublishCommitCell& operator=(const PublishCommitCell&) = delete("PublishCommitCell owns the channel identity; not copyable");
    PublishCommitCell(PublishCommitCell&&)                 = delete("interior atomic crosses thread boundary; cannot move");
    PublishCommitCell& operator=(PublishCommitCell&&)      = delete("interior atomic crosses thread boundary; cannot move");

    // Foreground-visible acquire load.  Pairs with the release store
    // performed inside bump_by (visible to friend code).  Any prior
    // publish-side write the bg performed before that bump_by becomes
    // visible to the fg under this load.
    [[nodiscard, gnu::pure]] uint64_t load_acquire() const noexcept {
        return value_.load(std::memory_order_acquire);
    }

    // Relaxed peek, for diagnostics only — no synchronization.
    [[nodiscard, gnu::pure]] uint64_t peek_relaxed() const noexcept {
        return value_.load(std::memory_order_relaxed);
    }

    // Convenience matching AtomicMonotonic<uint64_t>::get's contract
    // (acquire-load semantics) for callsite-by-callsite migration.
    [[nodiscard, gnu::pure]] uint64_t get() const noexcept {
        return load_acquire();
    }

    // Explicit memory-order surface — matches AtomicMonotonic::load.
    // Default is acquire (the safe choice for cross-thread reads
    // pairing with bump_by's release).  Diagnostic / introspection
    // sites that don't need synchronization may pass relaxed.
    [[nodiscard, gnu::pure]] uint64_t load(
        std::memory_order order = std::memory_order_acquire) const noexcept
    {
        return value_.load(order);
    }
};

// ─── Compile-time invariants ─────────────────────────────────────
//
// Cell occupies its own cache line (alignas(64)).  No move, no copy.
namespace publish_commit_detail {

struct ProbeTag {};
struct ProbeAuth {};

static_assert(std::is_trivially_destructible_v<
              PublishCommitCell<ProbeTag, ProbeAuth>>);
static_assert(!std::is_move_constructible_v<
              PublishCommitCell<ProbeTag, ProbeAuth>>);
static_assert(!std::is_copy_constructible_v<
              PublishCommitCell<ProbeTag, ProbeAuth>>);
// Cache-line alignment preserved.
static_assert(alignof(PublishCommitCell<ProbeTag, ProbeAuth>) == 64);

} // namespace publish_commit_detail

} // namespace crucible::safety
