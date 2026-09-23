#pragma once

// Two one-writer publication slots for a pointer.  PublishOnce takes
// exactly one publisher over its whole lifetime.  PublishSlot takes a
// publisher again and again and lets a consumer take the pointer back
// out.
//
// Old spelling: include/crucible/handles/PublishOnce.h.

#include <foundation/Platform.h>
#include <foundation/contracts/Post.h>
#include <foundation/diag/Catalog.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct publish_once;
struct publish_slot;
}  // namespace fixy::row_discipline

namespace fixy::handle {

// A contract assertion cannot carry this check.  Hot-path translation
// units build with the contract semantic set to ignore, where the
// assertion elides and a second publish quietly no-ops while the first
// pointer stays visible to observers.  This helper aborts regardless of
// the semantic the consuming translation unit chose.

[[noreturn]] CRUCIBLE_COLD inline void publish_once_double_publish_abort_() noexcept {
    using Tag = ::foundation::diag::PublishOnceDoublePublish;
    std::fprintf(stderr,
                 "crucible: fatal: %.*s\n"
                 "  description: %.*s\n"
                 "  remediation: %.*s\n",
                 static_cast<int>(Tag::name.size()), Tag::name.data(), static_cast<int>(Tag::description.size()),
                 Tag::description.data(), static_cast<int>(Tag::remediation.size()), Tag::remediation.data());
    std::abort();
}

// T names the pointee, never the pointer.  Both slots below add the
// star themselves and hand off a T*, so PublishOnce<Foo*> builds an
// atomic<Foo**> and publishes the address of a pointer variable rather
// than the handle the caller meant.  That variable is usually a local,
// and the double indirection is the whole defect: every observer reads
// a live pointer through a dangling one.
//
// T stays incomplete on purpose.  MerkleDag publishes a CompiledKernel
// through a forward declaration, so no part of this guard may ask for a
// size, an alignment or a member.  void is admitted for the same
// reason, as an opaque handle whose pointee type the publisher and the
// observer agree on out of band.
//
// A reference type is rejected here rather than left to std::atomic,
// where "forming pointer to reference type" names neither this header
// nor the mistake.
template <typename T>
concept PublishOncePointee = (!std::is_pointer_v<T>) && (!std::is_reference_v<T>);

// The guard this concept replaced read `is_pointer_v<T*> ||
// is_same_v<T, T>`, whose second disjunct is true for every T.  It
// admitted PublishOnce<int*> and enforced nothing.  These cells are the
// witness that the replacement discriminates: a guard that went
// tautological again would fail them here, in the header, rather than
// wait for a caller to be silently admitted.
static_assert(PublishOncePointee<int>);
static_assert(PublishOncePointee<void>);
static_assert(PublishOncePointee<const int>);
static_assert(PublishOncePointee<int[4]>);
static_assert(!PublishOncePointee<int*>, "a pointee that is itself a pointer is the T-versus-T* mistake");
static_assert(!PublishOncePointee<void*>);
static_assert(!PublishOncePointee<int&>, "T* over a reference type is ill-formed");
static_assert(!PublishOncePointee<int&&>);

namespace detail::publish_once_guard_probe {
// An incomplete type must pass the guard, because MerkleDag's slot is
// declared against one.
struct Incomplete;
static_assert(PublishOncePointee<Incomplete>);
}  // namespace detail::publish_once_guard_probe

// This class keeps the natural alignment of an atomic pointer while
// PublishSlot below pads to a whole cache line.  That slot is written
// again and again, so it must not share a line with anything.  This one
// is published once per instance, so its line is invalidated once and
// read from then on.  Dense structures hold thousands of these under a
// size-locked layout that padding would break.  An embedder whose
// neighbouring fields are written across threads applies the alignment
// at the embed site.
template <typename T>
class CRUCIBLE_OWNER PublishOnce {
    static_assert(PublishOncePointee<T>, "PublishOnce<T> hands off a T*, so T is the pointee.  PublishOnce<Foo*> "
                                         "publishes the address of a pointer variable, not the handle — write "
                                         "PublishOnce<Foo>.  A reference type has no pointer to form.");

    alignas(alignof(std::atomic<T*>)) std::atomic<T*> slot_{nullptr};

public:
    using row_discipline = ::fixy::row_discipline::publish_once;
    using row_payload = T;

    constexpr PublishOnce() noexcept = default;
    ~PublishOnce() = default;

    PublishOnce(const PublishOnce&) = delete("one publisher, one slot — copies would duplicate channel state");
    PublishOnce&
    operator=(const PublishOnce&) = delete("one publisher, one slot — copies would duplicate channel state");
    PublishOnce(PublishOnce&&) = delete("atomic is the channel identity");
    PublishOnce& operator=(PublishOnce&&) = delete("atomic is the channel identity");

    // A null pointer is the never-published state, so publishing one is
    // always a caller mistake.
    CRUCIBLE_INLINE void publish(T* ptr) noexcept pre(ptr != nullptr) {
        T* expected = nullptr;
        // The failure order is relaxed because a failure only has to be
        // detected, not synchronized with.  It means another publisher
        // already claimed the slot, and the branch below ends the
        // process.
        const bool claimed =
            slot_.compare_exchange_strong(expected, ptr, std::memory_order_release, std::memory_order_relaxed);
        if (!claimed) [[unlikely]] {
            publish_once_double_publish_abort_();
        }

        // Re-reading the slot here is not a race.  The exchange
        // succeeded, and any further publish from any thread fails its
        // exchange and ends the process above this line, so no writer
        // remains.  The read is of this thread's own store.
        CRUCIBLE_POST(0, slot_.load(std::memory_order_relaxed) == ptr);
    }

    [[nodiscard]] CRUCIBLE_INLINE T* observe() const noexcept { return slot_.load(std::memory_order_acquire); }

    // This load is relaxed, so a true result carries no ordering.  A
    // caller that goes on to dereference must reach the pointer through
    // observe.
    [[nodiscard]] CRUCIBLE_INLINE bool is_published() const noexcept {
        return slot_.load(std::memory_order_relaxed) != nullptr;
    }
};

static_assert(sizeof(PublishOnce<int>) == sizeof(std::atomic<int*>));
static_assert(sizeof(PublishOnce<void>) == sizeof(std::atomic<void*>));

template <typename T>
class CRUCIBLE_OWNER alignas(64) PublishSlot {
    // The same pointee-not-pointer contract as PublishOnce above, for
    // the same reason: this slot also adds the star itself.
    static_assert(PublishOncePointee<T>, "PublishSlot<T> hands off a T*, so T is the pointee.  PublishSlot<Foo*> "
                                         "publishes the address of a pointer variable, not the handle — write "
                                         "PublishSlot<Foo>.  A reference type has no pointer to form.");

    std::atomic<T*> slot_{nullptr};

public:
    using row_discipline = ::fixy::row_discipline::publish_slot;
    using row_payload = T;

    constexpr PublishSlot() noexcept = default;
    ~PublishSlot() = default;

    PublishSlot(const PublishSlot&) = delete("publication slot identity cannot be copied");
    PublishSlot& operator=(const PublishSlot&) = delete("publication slot identity cannot be copied");
    PublishSlot(PublishSlot&&) = delete("atomic slot is the channel identity");
    PublishSlot& operator=(PublishSlot&&) = delete("atomic slot is the channel identity");

    // This one carries no postcondition.  Another publisher, or a
    // consume, can replace the pointer before the witness reads it, so
    // the witness would be racy.
    CRUCIBLE_INLINE void publish(T* ptr) noexcept pre(ptr != nullptr) { slot_.store(ptr, std::memory_order_release); }

    [[nodiscard]] CRUCIBLE_INLINE T* observe() const noexcept { return slot_.load(std::memory_order_acquire); }

    [[nodiscard]] CRUCIBLE_INLINE T* consume() noexcept { return slot_.exchange(nullptr, std::memory_order_acq_rel); }

    [[nodiscard]] CRUCIBLE_INLINE bool has_pending() const noexcept {
        return slot_.load(std::memory_order_relaxed) != nullptr;
    }
};

static_assert(alignof(PublishSlot<int>) >= 64, "PublishSlot must be cache-line aligned: repeated publish/"
                                               "exchange traffic invalidates the consumer's cached line "
                                               "every iteration, so the slot must NOT share a line with "
                                               "unrelated embedder state.");
static_assert(alignof(PublishSlot<void>) >= 64);
static_assert(sizeof(PublishSlot<int>) >= 64);
static_assert(sizeof(PublishSlot<void>) >= 64);

}  // namespace fixy::handle
