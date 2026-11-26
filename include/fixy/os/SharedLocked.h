#pragma once

// A shared lock that owns its data: SharedLocked<T, Tag, Brand>.
//
// The lock holds a SharedPermissionPool of the exclusive permission of its
// data (foundation/permissions/SharedPermission.h) and one T, both private.
// No door gives a reference to the T outside a body:
//   shared.with_read(ctx, body)    a share of the pool for the time of body,
//                                  which gets T const&.  No value while the
//                                  exclusive is out, and then body does not
//                                  run
//   shared.with_write(ctx, body)   the lease of the exclusive for the time of
//                                  body, which gets T&.  No value while a
//                                  share or the lease is out
// The pool folds the count of the shares and the bit of the exclusive into
// one word, so a write cannot start while a read body runs, and no read can
// start while a write body runs.  A door never waits: it gives no value, and
// the caller tries again or takes another way.  The context owns Nondet,
// because the outcome of each door depends on the other threads
// (PoolCtx).
//
// The result of a body holds no borrow and fits a Record (LockedBody of
// fixy/os/SpinLock.h), so no reference to the T leaves the body through the
// return.  What stays open, as for each frame door (foundation/core/Frame.h):
// a body can store a borrow of the T in an object outside the body through a
// capture by reference, in a type that names no brand.  Also open for a T
// that moves and that gives views of memory it owns: a write body can assign
// such a T of the caller to the data, and a view that the caller took of it
// before the assignment stays live outside the lock.  A pinned T closes that
// route, as the region of LockedMmap does.
//
// The lock takes the exclusive permission of its tag with a fresh brand, so
// a share of one lock is no share of another lock of another root.
//
// A T that gives up what it holds (LeavesLock) has an end of its lock:
//   shared.end_lock(ctx)           takes the exclusive for good, and gives what
//                                  the T holds and the exclusive out.  No value
//                                  while a share or the lease is out, and then
//                                  the lock goes on
// The T gives it up only under the key of the end (SharedLockEndKey), which
// only the lock of that T builds.  So no write body takes what the T holds.  A
// lock that ended gives no value for ever: no door of it runs a body.  The lock
// object lives on, because another thread can hold a reference to it.

#include <fixy/os/SpinLock.h>
#include <foundation/Brand.h>
#include <foundation/Pinned.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Record.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

#include <type_traits>
#include <utility>

namespace fixy {

template <typename T, typename Tag, ::foundation::brand::IsFreshBrand Brand>
class SharedLocked;

// The key of the end of a lock over a T.  SharedLocked builds it only in
// end_lock, while that call holds the exclusive that leaves its pool for good,
// and lends it to the T of that lock for one call.  The key names the T, so
// another T that stores its key has no key of this one.
template <typename T>
class [[nodiscard]] SharedLockEndKey final {
    template <typename OtherT, typename Tag, ::foundation::brand::IsFreshBrand Brand>
    friend class SharedLocked;

    constexpr SharedLockEndKey() noexcept {}
    constexpr SharedLockEndKey(SharedLockEndKey const&) noexcept {}

public:
    SharedLockEndKey& operator=(SharedLockEndKey const&) = delete("a key of the end of a lock lives for one call");
};

// A T that gives up what it holds at the end of its lock: leave_lock takes the
// key and gives a value that holds no borrow of the T and of what the T keeps
// (HoldsNoBorrow of foundation/core/Choice.h).  The value can own a resource
// and hold its address, as a mapping does: an owner of the base holds no
// borrow when its type arguments hold none.
template <typename T>
concept LeavesLock = requires(T& data, SharedLockEndKey<T> const& key) {
    { data.leave_lock(key) } noexcept -> ::foundation::core::RecordElement;
    requires ::foundation::core::HoldsNoBorrow<decltype(data.leave_lock(key))>;
};

// What a T gives up at the end of its lock.
template <LeavesLock T>
using left_lock_t = decltype(std::declval<T&>().leave_lock(std::declval<SharedLockEndKey<T> const&>()));

template <typename T, typename Tag, ::foundation::brand::IsFreshBrand Brand>
class SharedLocked : public ::foundation::Pinned<SharedLocked<T, Tag, Brand>> {
public:
    using value_type = T;
    using tag_type = Tag;
    using brand_type = Brand;

    // The T that the arguments build, under the exclusive permission of the
    // tag, which the pool of the lock parks.
    template <class... Args>
        requires std::is_nothrow_constructible_v<T, Args&&...>
    explicit SharedLocked(::foundation::permissions::Permission<Tag, Brand>&& exclusive, Args&&... args) noexcept
        : pool_{std::move(exclusive)}, data_(static_cast<Args&&>(args)...) {}

    // A share of the pool for the time of body, which gets T const&: the
    // value of body, or Unit for a body that gives void.  No value while the
    // exclusive is out.  Complexity: one compare-exchange, O(1), and the
    // cost of body.
    template <typename Ctx, class Body>
        requires ::foundation::permissions::PoolCtx<Tag, Ctx> && spin::LockedBody<Body, T const>
    [[nodiscard]] ::foundation::core::Option<spin::locked_value_t<std::invoke_result_t<Body&&, T const&>>>
    with_read(Ctx const& ctx, Body&& body) noexcept {
        using Given = std::invoke_result_t<Body&&, T const&>;
        using Value = spin::locked_value_t<Given>;
        // The share lives until the end of this call, after body.
        auto const share = pool_.lend(ctx);
        if (share.is_none()) {
            return ::foundation::core::Option<Value>{::foundation::core::none};
        }
        T const& data = data_;
        if constexpr (std::is_void_v<Given>) {
            static_cast<Body&&>(body)(data);
            return ::foundation::core::Option<Value>::some(::foundation::core::Unit{});
        } else {
            return ::foundation::core::Option<Value>::some(static_cast<Body&&>(body)(data));
        }
    }

    // The lease of the exclusive for the time of body, which gets T&: the
    // value of body, or Unit for a body that gives void.  No value while a
    // share or the lease is out.  The pool parks the exclusive again after
    // body.  Complexity: one compare-exchange, O(1), and the cost of body.
    template <typename Ctx, class Body>
        requires ::foundation::permissions::PoolCtx<Tag, Ctx> && spin::LockedBody<Body, T>
    [[nodiscard]] ::foundation::core::Option<spin::locked_value_t<std::invoke_result_t<Body&&, T&>>>
    with_write(Ctx const& ctx, Body&& body) noexcept {
        using Given = std::invoke_result_t<Body&&, T&>;
        using Value = spin::locked_value_t<Given>;
        CRUCIBLE_TRY(lease, pool_.try_upgrade(ctx));
        if constexpr (std::is_void_v<Given>) {
            static_cast<Body&&>(body)(data_);
            pool_.deposit_exclusive(std::move(lease));
            return ::foundation::core::Option<Value>::some(::foundation::core::Unit{});
        } else {
            auto value = ::foundation::core::Option<Value>::some(static_cast<Body&&>(body)(data_));
            pool_.deposit_exclusive(std::move(lease));
            return value;
        }
    }

    // A body whose result holds a borrow binds to these overloads, so the
    // refusal gives its reason.
    template <typename Ctx, class Body>
        requires spin::LockedBorrowGivingBody<Body, T const>
    void with_read(Ctx const& ctx, Body&& body) = delete("the result of a body of a lock leaves the lock, so it holds "
                                                         "no reference and no address: give a plain value");
    template <typename Ctx, class Body>
        requires spin::LockedBorrowGivingBody<Body, T>
    void with_write(Ctx const& ctx, Body&& body) = delete("the result of a body of a lock leaves the lock, so it holds "
                                                          "no reference and no address: give a plain value");

    // Ends the lock.  The exclusive permission leaves the pool for good
    // (leave_pool), and the T gives up what it holds under the key of the end.
    // The pool keeps its exclusive-out state, so no door of the lock runs a
    // body after this call, and a second end gives no value.  No value while a
    // share or the lease is out, and then the lock goes on.  Complexity: one
    // compare-exchange, O(1), and the cost of leave_lock.
    //
    // The return type is the Option of Ended, and the body deduces it.  The
    // class instantiates each declared return type of a member, and left_lock_t
    // of a T with no end does not exist.
    template <typename Ctx>
        requires ::foundation::permissions::PoolCtx<Tag, Ctx> && LeavesLock<T>
    [[nodiscard]] auto end_lock(Ctx const& ctx) noexcept {
        using Ended = ::foundation::core::Record<left_lock_t<T>, ::foundation::permissions::Permission<Tag, Brand>>;
        using Given = ::foundation::core::Option<Ended>;
        CRUCIBLE_LET_ELSE(lease, pool_.try_upgrade(ctx), Given{::foundation::core::none});
        return Given::some(Ended{data_.leave_lock(SharedLockEndKey<T>{}), std::move(lease).leave_pool()});
    }

private:
    ::foundation::permissions::SharedPermissionPool<Tag, Brand> pool_;
    T data_;
};

}  // namespace fixy
