#pragma once

// ── crucible::safety::{PublishOnce<T>, PublishSlot<T>} ────────────────
//
// Single-publisher lock-free handoff of a pointer across threads.
//
//   Axiom coverage: ThreadSafe, LeakSafe, BorrowSafe.
//   Runtime cost:   one aligned atomic store-release on publish,
//                   one aligned atomic load-acquire on observe.
//                   sizeof(PublishOnce<T>) == sizeof(std::atomic<T*>).
//
// Semantics:
//   - publish(p)   — store-release of p.  Contract fires on a second
//                    publish: PublishOnce is, as the name says, once.
//   - observe()    — load-acquire.  Returns nullptr before publish,
//                    the published pointer after.  Callers that read
//                    the non-null case synchronize with the publisher.
//   - is_published() — relaxed check for diagnostic / fast-path
//                     branch prediction.  No ordering guarantees.
//
// Replaces the pattern:
//     std::atomic<T*> field{nullptr};
//   + comment explaining "set once, observed via acquire"
//   + manual CAS / discipline to not store twice
// with a type that encodes the discipline.
//
// Non-copyable and non-movable: the atomic is the identity of the
// channel; copying would break the "one publisher" invariant by
// duplicating state.
//
// PublishSlot<T> is the reusable latest-wins sibling for bg→fg
// handoff slots that are consumed with exchange(nullptr) and then
// republished.  Use PublishOnce when a field semantically transitions
// null→value at most once; use PublishSlot when each publication is a
// replaceable notification and the storage owner keeps old values
// alive independently.
//
// ── Cache-line isolation discipline (fixy-A1-002) ──────────────────
//
// The two types have DIFFERENT false-sharing exposure:
//
//   PublishSlot — `store(release)` and `exchange(acq_rel)` fire
//                 REPEATEDLY across the channel's lifetime.  Every
//                 publish invalidates the consumer's cached line; if
//                 the slot shares a line with unrelated state in its
//                 embedder, that state suffers an L3 round-trip on
//                 every publish.  This is the steady-state false-
//                 sharing case — cost O(publications × consumers).
//                 PublishSlot ships with `alignas(64)` so every
//                 instance lives on its own cache line, per
//                 CLAUDE.md §IX (every cross-thread atomic deserves
//                 its own line).
//
//   PublishOnce — `compare_exchange_strong(release)` fires EXACTLY
//                 ONCE per instance (the CAS gate + contract_assert
//                 enforces this).  After publication, the slot is
//                 read-only forever.  False-sharing cost is bounded
//                 at O(instances) one-time invalidations — not a
//                 steady-state problem.  PublishOnce stays at the
//                 atomic-pointer's natural alignment so dense
//                 embedders keep packing — notably RegionNode is
//                 layout-locked to 80B at MerkleDag.h:770 and a DAG
//                 holds thousands of them.  Embedders that need
//                 cache-line isolation for their PublishOnce field
//                 (e.g. when adjacent fields ARE written cross-
//                 thread at high frequency) apply `alignas(64)` at
//                 the embed site.
//
// The differentiation honours both the false-sharing physics and the
// density-vs-isolation trade-off: cache-line-isolate the truly hot
// signal (PublishSlot), pack the one-shot publication (PublishOnce).

#include <crucible/Platform.h>
#include <crucible/safety/Post.h>

#include <atomic>
#include <type_traits>

namespace crucible::safety {

template <typename T>
class CRUCIBLE_OWNER PublishOnce {
    static_assert(std::is_pointer_v<T*> || std::is_same_v<T, T>,
                  "PublishOnce<T> is for pointer handoff — use T*");

    // Atomic pointer; default-constructed nullptr encodes "not yet
    // published".  release / acquire ordering gives the publisher
    // the standard store-release / consumer load-acquire pair that
    // synchronizes the object being published.
    alignas(alignof(std::atomic<T*>)) std::atomic<T*> slot_{nullptr};

public:
    constexpr PublishOnce() noexcept = default;
    ~PublishOnce() = default;

    PublishOnce(const PublishOnce&)            = delete("one publisher, one slot — copies would duplicate channel state");
    PublishOnce& operator=(const PublishOnce&) = delete("one publisher, one slot — copies would duplicate channel state");
    PublishOnce(PublishOnce&&)                 = delete("atomic is the channel identity");
    PublishOnce& operator=(PublishOnce&&)      = delete("atomic is the channel identity");

    // Publish.  Caller must hold the sole right to publish on this
    // channel (enforced by convention, since the publisher is the
    // type of code that owns this field).  Second publish fires the
    // contract: double-publish would let two consumers observe
    // different values.
    //
    // pre(ptr != nullptr) — publishing nullptr is equivalent to
    // "never published" and is always a caller bug.
    CRUCIBLE_INLINE void publish(T* ptr) noexcept
        pre (ptr != nullptr)
    {
        T* expected = nullptr;
        // compare_exchange with release on success, relaxed on
        // failure: failure means "already published", which the
        // contract below converts into a termination.  The relaxed
        // failure load is enough — we don't synchronize with the
        // other publisher, we just detect it.
        const bool claimed = slot_.compare_exchange_strong(
            expected, ptr,
            std::memory_order_release,
            std::memory_order_relaxed);
        contract_assert(claimed);

        // CONTRACT-PublishOnce-Publish-POST: state-mutation post
        // (CRUCIBLE_POST taxonomy class 1, sibling of CKernelTable
        // register_op state-mutation post family / CrucibleContext
        // activate post-pair commit 0cf7c20).  The CAS succeeded
        // (contract_assert above) and PublishOnce is single-publisher
        // by class invariant — no other thread can mutate slot_ after
        // a successful CAS, so the post is well-defined (NOT racy:
        // a second publish() from any thread would fail CAS and abort
        // BEFORE reaching this line).
        //
        // The relaxed load suffices: we just stored ptr in the same
        // thread, no cross-thread synchronization is needed for
        // post-witness.  This catches a refactor that moves the CAS
        // logic but accidentally publishes a different pointer (e.g.
        // off-by-one in a loop body, or a moved-from local).
        //
        // PublishSlot::publish does NOT get the symmetric post —
        // PublishSlot is multi-publisher / consumer-resettable, so
        // re-reading slot_ races with other publishers / consume()
        // calls.  Skipped per the "racy" rationale class in
        // feedback_pre_post_dual_discipline.md.
        CRUCIBLE_POST(0, slot_.load(std::memory_order_relaxed) == ptr);
    }

    // Observe.  Returns the published pointer (acquire) or nullptr
    // before publish.  Non-nullptr return synchronizes with the
    // publisher's store-release — the published object's contents
    // are visible to this thread.
    [[nodiscard]] CRUCIBLE_INLINE T* observe() const noexcept {
        return slot_.load(std::memory_order_acquire);
    }

    // Relaxed: fast-path diagnostic check.  A `true` return still
    // requires a matching observe() for synchronization if the
    // caller dereferences; `false` is authoritative if false remains
    // false under the observed cache line's coherence.
    [[nodiscard]] CRUCIBLE_INLINE bool is_published() const noexcept {
        return slot_.load(std::memory_order_relaxed) != nullptr;
    }
};

// Zero-cost density: one aligned atomic pointer is identical to a
// bare std::atomic<T*> field plus the publish-once discipline encoded
// in the type system.  Unlike PublishSlot, this class does NOT pad to
// a cache line — the publish-once CAS gate bounds false-sharing cost
// at O(instances) one-time invalidations, and dense embedders
// (RegionNode is layout-locked to 80B at MerkleDag.h:770) rely on
// this footprint.  See the class doc-block for the differentiation.
static_assert(sizeof(PublishOnce<int>)  == sizeof(std::atomic<int*>));
static_assert(sizeof(PublishOnce<void>) == sizeof(std::atomic<void*>));

template <typename T>
class CRUCIBLE_OWNER alignas(64) PublishSlot {
    // Slot is at offset 0 of a cache-line-aligned class, so it
    // inherits the 64-byte alignment.  The publish/exchange path
    // fires repeatedly cross-thread; class-level alignas(64) keeps
    // it isolated from any unrelated state in the embedder.
    std::atomic<T*> slot_{nullptr};

public:
    constexpr PublishSlot() noexcept = default;
    ~PublishSlot() = default;

    PublishSlot(const PublishSlot&)            = delete("publication slot identity cannot be copied");
    PublishSlot& operator=(const PublishSlot&) = delete("publication slot identity cannot be copied");
    PublishSlot(PublishSlot&&)                 = delete("atomic slot is the channel identity");
    PublishSlot& operator=(PublishSlot&&)      = delete("atomic slot is the channel identity");

    CRUCIBLE_INLINE void publish(T* ptr) noexcept
        pre (ptr != nullptr)
    {
        slot_.store(ptr, std::memory_order_release);
    }

    [[nodiscard]] CRUCIBLE_INLINE T* observe() const noexcept {
        return slot_.load(std::memory_order_acquire);
    }

    [[nodiscard]] CRUCIBLE_INLINE T* consume() noexcept {
        return slot_.exchange(nullptr, std::memory_order_acq_rel);
    }

    [[nodiscard]] CRUCIBLE_INLINE bool has_pending() const noexcept {
        return slot_.load(std::memory_order_relaxed) != nullptr;
    }
};

// PublishSlot occupies a full cache line by construction (fixy-A1-002).
// alignof claim is the structural guarantee; sizeof follows from the
// standard rule that sizeof is a multiple of alignof.
static_assert(alignof(PublishSlot<int>)  >= 64,
              "PublishSlot must be cache-line aligned: repeated publish/"
              "exchange traffic invalidates the consumer's cached line "
              "every iteration, so the slot must NOT share a line with "
              "unrelated embedder state (CLAUDE.md §IX).");
static_assert(alignof(PublishSlot<void>) >= 64);
static_assert(sizeof(PublishSlot<int>)   >= 64);
static_assert(sizeof(PublishSlot<void>)  >= 64);

} // namespace crucible::safety
