#pragma once

// Wrapper-detection predicate for safety::Stale<T>.

#include <crucible/safety/Stale.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_stale_impl : std::false_type {
    using value_type = void;
    using semiring_type = void;
    using staleness_type = void;
};

template <typename T>
struct is_stale_impl<::crucible::safety::Stale<T>> : std::true_type {
    using value_type = T;
    using semiring_type = typename ::crucible::safety::Stale<T>::semiring_type;
    using staleness_type = typename ::crucible::safety::Stale<T>::staleness_t;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_stale_v =
    detail::is_stale_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsStale = is_stale_v<T>;

template <typename T>
    requires is_stale_v<T>
using stale_value_t =
    typename detail::is_stale_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_stale_v<T>
using stale_semiring_t =
    typename detail::is_stale_impl<std::remove_cvref_t<T>>::semiring_type;

template <typename T>
    requires is_stale_v<T>
using stale_staleness_t =
    typename detail::is_stale_impl<std::remove_cvref_t<T>>::staleness_type;

namespace detail::is_stale_self_test {

struct payload {};
using S_int = ::crucible::safety::Stale<int>;
using S_payload = ::crucible::safety::Stale<payload>;

struct LookalikeStale {
    using value_type = int;
    using staleness_t = unsigned long long;
};

static_assert( is_stale_v<S_int>);
static_assert( is_stale_v<S_payload>);

static_assert( is_stale_v<S_int&>);
static_assert( is_stale_v<S_int&&>);
static_assert( is_stale_v<S_int const>);
static_assert( is_stale_v<S_int const&>);
static_assert( is_stale_v<S_int volatile>);

static_assert(!is_stale_v<int>);
static_assert(!is_stale_v<int*>);
static_assert(!is_stale_v<S_int*>);
static_assert(!is_stale_v<void>);
static_assert(!is_stale_v<LookalikeStale>);

static_assert( IsStale<S_int>);
static_assert( IsStale<S_payload const&>);
static_assert(!IsStale<int>);

static_assert(std::is_same_v<stale_value_t<S_int>, int>);
static_assert(std::is_same_v<stale_value_t<S_payload const&>, payload>);
static_assert(std::is_same_v<
    stale_semiring_t<S_int>,
    ::crucible::algebra::lattices::StalenessSemiring>);
static_assert(std::is_same_v<
    stale_staleness_t<S_int>,
    ::crucible::algebra::lattices::StalenessSemiring::element_type>);

inline bool runtime_smoke_test() noexcept {
    volatile int const cap = 4;
    bool ok = true;
    for (int i = 0; i < cap; ++i) {
        ok = ok && is_stale_v<S_int>;
        ok = ok && is_stale_v<S_payload const&>;
        ok = ok && !is_stale_v<int>;
        ok = ok && !is_stale_v<LookalikeStale>;
        ok = ok && IsStale<S_int&&>;
    }
    return ok;
}

}  // namespace detail::is_stale_self_test

}  // namespace crucible::safety::extract
