#pragma once

#include <crucible/safety/OwnedRegion.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_owned_region_impl : std::false_type {
    using value_type = void;
    using tag_type = void;
};

template <typename T, typename Tag>
struct is_owned_region_impl<::crucible::safety::OwnedRegion<T, Tag>> : std::true_type {
    using value_type = T;
    using tag_type = Tag;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_owned_region_v = detail::is_owned_region_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsOwnedRegion = is_owned_region_v<T>;

// The two extractors are constrained rather than left to the primary
// template, which would hand back void for an unrelated argument instead of
// failing.

template <typename T>
    requires is_owned_region_v<T>
using owned_region_value_t = typename detail::is_owned_region_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_owned_region_v<T>
using owned_region_tag_t = typename detail::is_owned_region_impl<std::remove_cvref_t<T>>::tag_type;

namespace detail::is_owned_region_self_test {

struct test_tag_a {};
struct test_tag_b {};

using OR_int_a = ::crucible::safety::OwnedRegion<int, test_tag_a>;
using OR_double_a = ::crucible::safety::OwnedRegion<double, test_tag_a>;
using OR_int_b = ::crucible::safety::OwnedRegion<int, test_tag_b>;

static_assert(is_owned_region_v<OR_int_a>);
static_assert(is_owned_region_v<OR_double_a>);
static_assert(is_owned_region_v<OR_int_b>);

static_assert(is_owned_region_v<OR_int_a&>);
static_assert(is_owned_region_v<OR_int_a&&>);
static_assert(is_owned_region_v<OR_int_a const&>);
static_assert(is_owned_region_v<OR_int_a const>);
static_assert(is_owned_region_v<OR_int_a const&&>);

static_assert(!is_owned_region_v<int>);
static_assert(!is_owned_region_v<int*>);
static_assert(!is_owned_region_v<int&>);
static_assert(!is_owned_region_v<void>);
static_assert(!is_owned_region_v<test_tag_a>);

struct LookalikeRegion {
    int* base;
    std::size_t count;
};
static_assert(!is_owned_region_v<LookalikeRegion>);

static_assert(IsOwnedRegion<OR_int_a>);
static_assert(IsOwnedRegion<OR_int_a&&>);
static_assert(!IsOwnedRegion<int>);

static_assert(std::is_same_v<owned_region_value_t<OR_int_a>, int>);
static_assert(std::is_same_v<owned_region_value_t<OR_double_a>, double>);
static_assert(std::is_same_v<owned_region_tag_t<OR_int_a>, test_tag_a>);
static_assert(std::is_same_v<owned_region_tag_t<OR_int_b>, test_tag_b>);

static_assert(std::is_same_v<owned_region_value_t<OR_int_a const&>, int>);
static_assert(std::is_same_v<owned_region_tag_t<OR_int_a&&>, test_tag_a>);

static_assert(std::is_same_v<owned_region_value_t<OR_int_a>, owned_region_value_t<OR_int_b>>);
static_assert(!std::is_same_v<owned_region_tag_t<OR_int_a>, owned_region_tag_t<OR_int_b>>);

}  // namespace detail::is_owned_region_self_test

inline bool is_owned_region_smoke_test() noexcept {
    using namespace detail::is_owned_region_self_test;

    // The volatile bound defeats constant folding, so the trait reads survive
    // dead-code elimination.
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_owned_region_v<OR_int_a>;
        ok = ok && !is_owned_region_v<int>;
        ok = ok && IsOwnedRegion<OR_int_a&&>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
