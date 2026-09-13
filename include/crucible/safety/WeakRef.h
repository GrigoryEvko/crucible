#pragma once

// Non-owning, nullable reference to an object owned elsewhere.  The
// slot starts empty, holds a borrowed pointer, and may be evicted.
//
// There is no control block, so expiry of the referent is not detected.
// The check this type provides is a null check and nothing more:
// try_get returns a pointer the caller has to inspect, and the
// unconditional accessors carry a non-null precondition so a missing
// check aborts instead of going quietly wrong.  Keeping the referent
// alive for as long as a WeakRef points at it remains the owner's
// obligation.

#include <crucible/Platform.h>
#include <crucible/safety/Pre.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

template <class T>
    requires(std::is_object_v<T>)
class [[nodiscard]] WeakRef {
public:
    using element_type = T;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::WeakRef"; }

private:
    T* ptr_ = nullptr;

    struct from_raw_tag_t {};
    constexpr WeakRef(from_raw_tag_t, T* p) noexcept : ptr_{p} {}

public:
    constexpr WeakRef() noexcept = default;

    constexpr explicit WeakRef(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept : ptr_{&ref} {}

    // Null is a valid input here, so there is no precondition.
    [[nodiscard]] static constexpr WeakRef from_raw(T* p CRUCIBLE_LIFETIMEBOUND) noexcept {
        return WeakRef{from_raw_tag_t{}, p};
    }

    constexpr WeakRef(WeakRef const&) = default;
    constexpr WeakRef(WeakRef&&) = default;
    constexpr WeakRef& operator=(WeakRef const&) = default;
    constexpr WeakRef& operator=(WeakRef&&) = default;
    ~WeakRef() = default;

    [[nodiscard]] constexpr bool has_value() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] constexpr T* try_get() const noexcept { return ptr_; }

    [[nodiscard]] constexpr T& get() const noexcept {
        CRUCIBLE_PRE(ptr_ != nullptr);
        return *ptr_;
    }
    [[nodiscard]] constexpr T& operator*() const noexcept {
        CRUCIBLE_PRE(ptr_ != nullptr);
        return *ptr_;
    }
    [[nodiscard]] constexpr T* operator->() const noexcept {
        CRUCIBLE_PRE(ptr_ != nullptr);
        return ptr_;
    }

    constexpr void reset() noexcept { ptr_ = nullptr; }

    [[nodiscard]] friend constexpr bool operator==(WeakRef a, WeakRef b) noexcept { return a.ptr_ == b.ptr_; }
};

static_assert(sizeof(WeakRef<int>) == sizeof(int*));
static_assert(alignof(WeakRef<int>) == alignof(int*));
static_assert(std::is_trivially_copyable_v<WeakRef<int>>);
static_assert(std::is_trivially_destructible_v<WeakRef<int>>);

static_assert(std::is_default_constructible_v<WeakRef<int>>);

static_assert(!std::is_convertible_v<WeakRef<int>, int*>);
static_assert(!std::is_convertible_v<int&, WeakRef<int>>);

namespace detail::weak_ref_self_test {

[[nodiscard]] consteval bool default_is_empty() noexcept {
    WeakRef<int> w{};
    return !w.has_value() && !static_cast<bool>(w) && w.try_get() == nullptr;
}
static_assert(default_is_empty());

[[nodiscard]] consteval bool binds_and_derefs() noexcept {
    int x = 42;
    WeakRef<int> w{x};
    return w.has_value() && static_cast<bool>(w) && w.try_get() == &x && w.get() == 42 && *w == 42;
}
static_assert(binds_and_derefs());

[[nodiscard]] consteval bool arrow_reaches_member() noexcept {
    struct Pair {
        int a;
        int b;
    };
    Pair p{7, 9};
    WeakRef<Pair> w{p};
    return w->a == 7 && w->b == 9;
}
static_assert(arrow_reaches_member());

[[nodiscard]] consteval bool from_raw_is_nullable() noexcept {
    WeakRef<int> empty = WeakRef<int>::from_raw(nullptr);
    int x = 5;
    WeakRef<int> full = WeakRef<int>::from_raw(&x);
    return !empty.has_value() && full.has_value() && full.try_get() == &x;
}
static_assert(from_raw_is_nullable());

[[nodiscard]] consteval bool reset_evicts() noexcept {
    int x = 1;
    WeakRef<int> w{x};
    if (!w.has_value()) return false;
    w.reset();
    return !w.has_value() && w.try_get() == nullptr;
}
static_assert(reset_evicts());

// x and y hold the same value in different objects.
[[nodiscard]] consteval bool identity_equality_and_copy() noexcept {
    int x = 3;
    int y = 3;
    WeakRef<int> a{x};
    WeakRef<int> b = a;
    WeakRef<int> c{y};
    return a == b && !(a == c) && a.try_get() == b.try_get() && WeakRef<int>{} == WeakRef<int>{};
}
static_assert(identity_equality_and_copy());

static_assert(WeakRef<int>::wrapper_kind() == "structural::WeakRef");

inline void runtime_smoke_test() {
    WeakRef<int> empty{};
    if (empty.has_value() || static_cast<bool>(empty)) std::abort();
    if (empty.try_get() != nullptr) std::abort();

    volatile int seed = 77;
    int box = static_cast<int>(seed);
    WeakRef<int> w{box};
    if (!w.has_value() || !static_cast<bool>(w)) std::abort();
    if (w.try_get() != &box) std::abort();
    if (w.get() != box) std::abort();
    if (*w != box) std::abort();

    struct Pair {
        int a;
        int b;
    };
    Pair p{static_cast<int>(seed), static_cast<int>(seed) + 1};
    WeakRef<Pair> wp{p};
    if (wp->a != box || wp->b != box + 1) std::abort();

    w.reset();
    if (w.has_value() || w.try_get() != nullptr) std::abort();

    WeakRef<int> fr_null = WeakRef<int>::from_raw(nullptr);
    if (fr_null.has_value()) std::abort();
    WeakRef<int> fr_full = WeakRef<int>::from_raw(&box);
    if (!fr_full.has_value() || fr_full.try_get() != &box) std::abort();

    WeakRef<int> a{box};
    WeakRef<int> b = a;
    if (!(a == b) || a.try_get() != b.try_get()) std::abort();
}

}  // namespace detail::weak_ref_self_test

}  // namespace crucible::safety
