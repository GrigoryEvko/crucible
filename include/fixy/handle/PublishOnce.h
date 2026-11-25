#pragma once

// Three one-writer publication slots for an object.  PublishOnce takes
// exactly one publisher over its whole lifetime.  BrandedPublishOnce takes
// one publisher too, and only a Ref of its brand.  PublishSlot takes a
// publisher again and again and lets a consumer take the pointer back
// out.

#include <foundation/Brand.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/contracts/Post.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/diag/tags/Aborts.h>
#include <foundation/os/Abort.h>

#include <atomic>
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
    ::foundation::core::report(::foundation::core::Sink::Err,
                               "crucible: fatal: {}\n"
                               "  description: {}\n"
                               "  remediation: {}\n",
                               ::foundation::core::TextView{Tag::name}, ::foundation::core::TextView{Tag::description},
                               ::foundation::core::TextView{Tag::remediation});
    ::foundation::os::abort_process();
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
// A publish takes a Ref (foundation/core/Ref.h), so T is a target of a
// Ref or void.  A reference type, an array and a volatile type are no
// target of a Ref, so the guard refuses them here, and not in the
// overload set of publish or in std::atomic, where "forming pointer to
// reference type" names neither this header nor the mistake.
template <typename T>
concept PublishOncePointee = (!std::is_pointer_v<T>) && (std::is_void_v<T> || ::foundation::core::RefTarget<T>);

// The targets of a Ref that a slot of T takes: each target whose address
// converts to a T*, such as a Foo for a slot of a Foo const, or each
// object for a slot of void.
template <typename Target, typename T>
concept PublishesInto = std::is_convertible_v<Target*, T*>;

// The guard this concept replaced read `is_pointer_v<T*> ||
// is_same_v<T, T>`, whose second disjunct is true for every T.  It
// admitted PublishOnce<int*> and enforced nothing.  The cells in the
// check file of this header are the witness that the replacement
// discriminates: a guard that went tautological again would fail them in
// the default build, rather than wait for a caller to be silently
// admitted.

// This class keeps the natural alignment of an atomic pointer while
// PublishSlot below pads to a whole cache line.  That slot is written
// again and again, so it must not share a line with anything.  This one
// is published once per instance, so its line is invalidated once and
// read from then on.  Dense structures hold thousands of these under a
// size-locked layout that padding would break.  An embedder whose
// neighbouring fields are written across threads applies the alignment
// at the embed site.
//
// Each observer gets the pointer that the publisher stored, so the slot
// refers to the object of T as a reference does, and it states refers{}
// (foundation/Transfer.h).  Two threads can read a PublishOnce<T const>
// when two threads can read a T, and a slot of a T that is not const asks
// that two threads can write the T.
//
// Use this slot only in the base, and only where no brand names the owner of
// the object: for an object that the caller owns and gives by a reference,
// such as the resource of LazyEstablishedChannel, and for a void payload.
// The caller keeps such an object alive.  An observer gets a raw pointer, and
// the type names no owner, so no product code takes this slot.  Product code
// takes BrandedPublishOnce below.
template <typename T>
class CRUCIBLE_OWNER[[= ::foundation::transfer::refers{}]] PublishOnce {
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

    // A null pointer is the never-published state.  The slot takes the
    // address of the object of a Ref, and a Ref is never null, so no
    // publish stores that state, and the publish needs no test.
    template <::foundation::core::RefTarget Target, ::foundation::brand::IsBrand Brand>
        requires PublishesInto<Target, T>
    CRUCIBLE_INLINE void publish(::foundation::core::Ref<Target, Brand> object) noexcept {
        T* const ptr = __builtin_addressof(*object);
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

    // Any thread asks, and the publisher is a different thread, so the load
    // takes acquire order.  A caller that goes on to dereference reaches the
    // pointer through observe.
    [[nodiscard]] CRUCIBLE_INLINE bool is_published() const noexcept {
        return slot_.load(std::memory_order_acquire) != nullptr;
    }
};

// The targets of a BrandedPublishOnce: a target of a Ref, and the slot names
// the pointee and not the pointer, as PublishOnce does.
template <typename T>
concept BrandedPublishOncePointee = PublishOncePointee<T> && ::foundation::core::RefTarget<T>;

// A slot of one publisher that holds only a Ref of its brand.  A PublishOnce
// takes a Ref of each brand, so its type does not tell which owner keeps the
// object.  This slot names the brand, so an object that holds one, such as a
// node of an arena, names the owner of what it borrows.  An observer gets the
// Ref with the brand, and no raw pointer.
//
// Use this slot wherever a brand names the owner of the published object,
// such as the tag of an arena or of a pool.  Product code takes only this
// slot.  A void payload has no Ref, so it stays with PublishOnce.
//
// The cell is one atomic word, the word of an Option of the Ref, and the empty
// word is the never-published state.  So the size and the alignment are those
// of a pointer, as in PublishOnce.  The cell holds each transfer property when
// its Ref is Sendable (foundation/core/Atomic.h): two threads can observe a
// slot of a T const when two threads can read a T.
template <typename T, ::foundation::brand::IsFreshBrand Brand>
    requires BrandedPublishOncePointee<T>
class CRUCIBLE_OWNER BrandedPublishOnce final : ::foundation::Pinned<BrandedPublishOnce<T, Brand>> {
public:
    using brand_type = Brand;
    using row_discipline = ::fixy::row_discipline::publish_once;
    using row_payload = T;

private:
    using Held = ::foundation::core::Ref<T, Brand>;
    using Word = ::foundation::core::Option<Held>;

    ::foundation::core::Atomic<Word> slot_{};

public:
    constexpr BrandedPublishOnce() noexcept = default;
    ~BrandedPublishOnce() = default;

    // Publishes the object.  A Ref is never null, so no publish stores the
    // never-published state.  A second publish ends the process in each build.
    CRUCIBLE_INLINE void publish(Held object) noexcept {
        bool const is_claimed =
            slot_.cas_acq_rel(Word{::foundation::core::none}, ::foundation::core::some(object)).is_ok();
        if (!is_claimed) [[unlikely]] {
            publish_once_double_publish_abort_();
        }
    }

    // The object of the publisher, or no value before the publish.
    [[nodiscard]] CRUCIBLE_INLINE Word observe() const noexcept { return slot_.load_acquire(); }

    // Any thread asks, and the publisher is a different thread, so the load
    // takes acquire order.
    [[nodiscard]] CRUCIBLE_INLINE bool is_published() const noexcept { return slot_.load_acquire().is_some(); }
};

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

    // The slot takes the address of the object of a Ref, as PublishOnce
    // does, so no publish stores the null pointer of the empty slot.  This
    // one carries no postcondition.  Another publisher, or a consume, can
    // replace the pointer before the witness reads it, so the witness would
    // be racy.
    template <::foundation::core::RefTarget Target, ::foundation::brand::IsBrand Brand>
        requires PublishesInto<Target, T>
    CRUCIBLE_INLINE void publish(::foundation::core::Ref<Target, Brand> object) noexcept {
        T* const ptr = __builtin_addressof(*object);
        slot_.store(ptr, std::memory_order_release);
    }

    [[nodiscard]] CRUCIBLE_INLINE T* observe() const noexcept { return slot_.load(std::memory_order_acquire); }

    [[nodiscard]] CRUCIBLE_INLINE T* consume() noexcept { return slot_.exchange(nullptr, std::memory_order_acq_rel); }

    // Any thread asks, and a publisher or a consumer on a different thread
    // changes the slot, so the load takes acquire order.
    [[nodiscard]] CRUCIBLE_INLINE bool has_pending() const noexcept {
        return slot_.load(std::memory_order_acquire) != nullptr;
    }
};

}  // namespace fixy::handle
