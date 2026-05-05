#pragma once

// Wrapper-detection predicate for safety::Secret<T>.
//
// Secret<T> does not encode a declassification policy in the wrapper type;
// policies are named at the consuming call site via declassify<Policy>().
// This detector therefore extracts only the classified payload type.

#include <crucible/safety/Secret.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_secret_impl : std::false_type {
    using value_type = void;
};

template <typename T>
struct is_secret_impl<::crucible::safety::Secret<T>> : std::true_type {
    using value_type = T;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_secret_v =
    detail::is_secret_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSecret = is_secret_v<T>;

template <typename T>
    requires is_secret_v<T>
using secret_value_t =
    typename detail::is_secret_impl<std::remove_cvref_t<T>>::value_type;

namespace detail::is_secret_self_test {

struct payload {};
using S_int = ::crucible::safety::Secret<int>;
using S_payload = ::crucible::safety::Secret<payload>;

struct LookalikeSecret {
    using value_type = int;
    int payload;
};

static_assert( is_secret_v<S_int>);
static_assert( is_secret_v<S_payload>);

static_assert( is_secret_v<S_int&>);
static_assert( is_secret_v<S_int&&>);
static_assert( is_secret_v<S_int const>);
static_assert( is_secret_v<S_int const&>);
static_assert( is_secret_v<S_int volatile>);

static_assert(!is_secret_v<int>);
static_assert(!is_secret_v<int*>);
static_assert(!is_secret_v<S_int*>);
static_assert(!is_secret_v<void>);
static_assert(!is_secret_v<LookalikeSecret>);

static_assert( IsSecret<S_int>);
static_assert( IsSecret<S_payload const&>);
static_assert(!IsSecret<int>);

static_assert(std::is_same_v<secret_value_t<S_int>, int>);
static_assert(std::is_same_v<secret_value_t<S_payload const&>, payload>);

inline bool runtime_smoke_test() noexcept {
    volatile int const cap = 4;
    bool ok = true;
    for (int i = 0; i < cap; ++i) {
        ok = ok && is_secret_v<S_int>;
        ok = ok && is_secret_v<S_payload const&>;
        ok = ok && !is_secret_v<int>;
        ok = ok && !is_secret_v<LookalikeSecret>;
        ok = ok && IsSecret<S_int&&>;
    }
    return ok;
}

}  // namespace detail::is_secret_self_test

}  // namespace crucible::safety::extract
