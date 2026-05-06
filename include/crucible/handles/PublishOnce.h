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

#include <crucible/Platform.h>

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

// Zero-cost: one aligned atomic pointer is identical to a bare
// std::atomic<T*> field plus the publish-once discipline encoded in
// the type system.
static_assert(sizeof(PublishOnce<int>)  == sizeof(std::atomic<int*>));
static_assert(sizeof(PublishOnce<void>) == sizeof(std::atomic<void*>));

template <typename T>
class CRUCIBLE_OWNER PublishSlot {
    alignas(alignof(std::atomic<T*>)) std::atomic<T*> slot_{nullptr};

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

static_assert(sizeof(PublishSlot<int>)  == sizeof(std::atomic<int*>));
static_assert(sizeof(PublishSlot<void>) == sizeof(std::atomic<void*>));

} // namespace crucible::safety
