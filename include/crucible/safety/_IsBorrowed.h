#pragma once

#include <crucible/safety/_Borrowed.h>

#include <cstdlib>
#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_borrowed_impl : std::false_type {
    using element_type = void;
    using source_type = void;
};

template <typename T, typename Source>
struct is_borrowed_impl<::crucible::safety::Borrowed<T, Source>> : std::true_type {
    using element_type = T;
    using source_type = Source;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_borrowed_v = detail::is_borrowed_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsBorrowed = is_borrowed_v<T>;

template <typename T>
    requires is_borrowed_v<T>
using borrowed_value_t = typename detail::is_borrowed_impl<std::remove_cvref_t<T>>::element_type;

template <typename T>
    requires is_borrowed_v<T>
using borrowed_source_t = typename detail::is_borrowed_impl<std::remove_cvref_t<T>>::source_type;

namespace detail::is_borrowed_self_test {

struct OwnerA {
    int dummy = 0;
};
struct OwnerB {
    int dummy = 0;
};

using B_A = ::crucible::safety::Borrowed<int, OwnerA>;
using B_B = ::crucible::safety::Borrowed<const char, OwnerB>;
using B_dbl = ::crucible::safety::Borrowed<double, OwnerA>;

static_assert(is_borrowed_v<B_A>);
static_assert(is_borrowed_v<B_B>);
static_assert(is_borrowed_v<B_dbl>);

static_assert(is_borrowed_v<B_A&>);
static_assert(is_borrowed_v<B_A const&>);
static_assert(is_borrowed_v<B_A&&>);

static_assert(!is_borrowed_v<int>);
static_assert(!is_borrowed_v<int*>);
static_assert(!is_borrowed_v<std::span<int>>,
              "A bare std::span must not satisfy is_borrowed_v. It is the underlying carrier, not "
              "the wrapper. Without this rejection, untagged spans slip through concept gates "
              "that expect a Borrowed.");
static_assert(!is_borrowed_v<B_A*>);
static_assert(!is_borrowed_v<void>);

struct LookalikeBorrowed {
    std::span<int> span_;
};
static_assert(!is_borrowed_v<LookalikeBorrowed>,
              "is_borrowed_v must reject lookalikes. If this fires, the partial specialization "
              "has weakened to duck-typing and downstream concept overloads will misfire on user "
              "types whose member shape happens to match the internal layout of Borrowed.");

static_assert(IsBorrowed<B_A>);
static_assert(!IsBorrowed<int>);

static_assert(std::is_same_v<borrowed_value_t<B_A>, int>);
static_assert(std::is_same_v<borrowed_value_t<B_B>, const char>);
static_assert(std::is_same_v<borrowed_source_t<B_A>, OwnerA>);
static_assert(std::is_same_v<borrowed_source_t<B_B>, OwnerB>);
static_assert(std::is_same_v<borrowed_source_t<B_dbl>, OwnerA>);

inline void runtime_smoke_test() {
    if (!is_borrowed_v<B_A>) std::abort();
    if (is_borrowed_v<int>) std::abort();
    if (!IsBorrowed<B_B>) std::abort();
    if (IsBorrowed<std::span<int>>) std::abort();
    if (!std::is_same_v<borrowed_value_t<B_dbl>, double>) std::abort();
    if (!std::is_same_v<borrowed_source_t<B_dbl>, OwnerA>) std::abort();
}

}  // namespace detail::is_borrowed_self_test

}  // namespace crucible::safety::extract
