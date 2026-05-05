#pragma once

// Wrapper-detection predicate for safety::Linear<T>.
//
// This follows the safety/Is*.h convention: exact template-specialization
// detection after cv-ref stripping, constrained extractor aliases, local
// compile-time witnesses, and a runtime smoke hook for the sentinel TU.

#include <crucible/safety/Linear.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_linear_impl : std::false_type {
    using value_type = void;
};

template <typename T>
struct is_linear_impl<::crucible::safety::Linear<T>> : std::true_type {
    using value_type = T;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_linear_v =
    detail::is_linear_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsLinear = is_linear_v<T>;

template <typename T>
    requires is_linear_v<T>
using linear_value_t =
    typename detail::is_linear_impl<std::remove_cvref_t<T>>::value_type;

namespace detail::is_linear_self_test {

struct tag {};
using L_int = ::crucible::safety::Linear<int>;
using L_ptr = ::crucible::safety::Linear<void*>;
using L_tag = ::crucible::safety::Linear<tag>;

struct DerivedLinear : L_int {
    using L_int::L_int;
};

struct LookalikeLinear {
    using value_type = int;
    int payload;
};

static_assert( is_linear_v<L_int>);
static_assert( is_linear_v<L_ptr>);
static_assert( is_linear_v<L_tag>);

static_assert( is_linear_v<L_int&>);
static_assert( is_linear_v<L_int&&>);
static_assert( is_linear_v<L_int const>);
static_assert( is_linear_v<L_int const&>);
static_assert( is_linear_v<L_int volatile>);
static_assert( is_linear_v<L_int const volatile>);

static_assert(!is_linear_v<int>);
static_assert(!is_linear_v<int*>);
static_assert(!is_linear_v<L_int*>);
static_assert(!is_linear_v<void>);
static_assert(!is_linear_v<LookalikeLinear>);
static_assert(!is_linear_v<DerivedLinear>);

static_assert( IsLinear<L_int>);
static_assert( IsLinear<L_int&&>);
static_assert(!IsLinear<int>);
static_assert(!IsLinear<DerivedLinear>);

static_assert(std::is_same_v<linear_value_t<L_int>, int>);
static_assert(std::is_same_v<linear_value_t<L_ptr>, void*>);
static_assert(std::is_same_v<linear_value_t<L_tag const&>, tag>);

inline bool runtime_smoke_test() noexcept {
    volatile int const cap = 4;
    bool ok = true;
    for (int i = 0; i < cap; ++i) {
        ok = ok && is_linear_v<L_int>;
        ok = ok && is_linear_v<L_int const&>;
        ok = ok && !is_linear_v<int>;
        ok = ok && !is_linear_v<DerivedLinear>;
        ok = ok && IsLinear<L_tag&&>;
    }
    return ok;
}

}  // namespace detail::is_linear_self_test

}  // namespace crucible::safety::extract
