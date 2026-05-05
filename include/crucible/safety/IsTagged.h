#pragma once

// Wrapper-detection predicate for safety::Tagged<T, Tag>.

#include <crucible/safety/Tagged.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_tagged_impl : std::false_type {
    using value_type = void;
    using tag_type = void;
};

template <typename T, typename Tag>
struct is_tagged_impl<::crucible::safety::Tagged<T, Tag>>
    : std::true_type
{
    using value_type = T;
    using tag_type = Tag;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_tagged_v =
    detail::is_tagged_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsTagged = is_tagged_v<T>;

template <typename T>
    requires is_tagged_v<T>
using tagged_value_t =
    typename detail::is_tagged_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_tagged_v<T>
using tagged_tag_t =
    typename detail::is_tagged_impl<std::remove_cvref_t<T>>::tag_type;

namespace detail::is_tagged_self_test {

struct tag_a {};
struct tag_b {};
using T_int_a = ::crucible::safety::Tagged<int, tag_a>;
using T_int_b = ::crucible::safety::Tagged<int, tag_b>;
using T_double_a = ::crucible::safety::Tagged<double, tag_a>;

struct LookalikeTagged {
    using value_type = int;
    using tag_type = tag_a;
};

static_assert( is_tagged_v<T_int_a>);
static_assert( is_tagged_v<T_int_b>);
static_assert( is_tagged_v<T_double_a>);

static_assert( is_tagged_v<T_int_a&>);
static_assert( is_tagged_v<T_int_a&&>);
static_assert( is_tagged_v<T_int_a const>);
static_assert( is_tagged_v<T_int_a const&>);
static_assert( is_tagged_v<T_int_a volatile>);

static_assert(!is_tagged_v<int>);
static_assert(!is_tagged_v<int*>);
static_assert(!is_tagged_v<T_int_a*>);
static_assert(!is_tagged_v<void>);
static_assert(!is_tagged_v<LookalikeTagged>);

static_assert( IsTagged<T_int_a>);
static_assert( IsTagged<T_int_b const&>);
static_assert(!IsTagged<int>);

static_assert(std::is_same_v<tagged_value_t<T_int_a>, int>);
static_assert(std::is_same_v<tagged_value_t<T_double_a>, double>);
static_assert(std::is_same_v<tagged_tag_t<T_int_a>, tag_a>);
static_assert(std::is_same_v<tagged_tag_t<T_int_b const&>, tag_b>);
static_assert(!std::is_same_v<tagged_tag_t<T_int_a>, tagged_tag_t<T_int_b>>);

inline bool runtime_smoke_test() noexcept {
    volatile int const cap = 4;
    bool ok = true;
    for (int i = 0; i < cap; ++i) {
        ok = ok && is_tagged_v<T_int_a>;
        ok = ok && is_tagged_v<T_int_b const&>;
        ok = ok && !is_tagged_v<int>;
        ok = ok && !is_tagged_v<LookalikeTagged>;
        ok = ok && IsTagged<T_double_a&&>;
    }
    return ok;
}

}  // namespace detail::is_tagged_self_test

}  // namespace crucible::safety::extract
