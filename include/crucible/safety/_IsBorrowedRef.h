#pragma once

#include <crucible/safety/_Borrowed.h>

#include <cstdlib>
#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_borrowed_ref_impl : std::false_type {
    using element_type = void;
};

template <typename T>
struct is_borrowed_ref_impl<::crucible::safety::BorrowedRef<T>> : std::true_type {
    using element_type = T;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_borrowed_ref_v = detail::is_borrowed_ref_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsBorrowedRef = is_borrowed_ref_v<T>;

template <typename T>
    requires is_borrowed_ref_v<T>
using borrowed_ref_value_t = typename detail::is_borrowed_ref_impl<std::remove_cvref_t<T>>::element_type;

namespace detail::is_borrowed_ref_self_test {

struct Holder {
    int v = 0;
};

using R_int = ::crucible::safety::BorrowedRef<int>;
using R_holder = ::crucible::safety::BorrowedRef<Holder>;
using R_double = ::crucible::safety::BorrowedRef<double>;
using R_const = ::crucible::safety::BorrowedRef<const int>;

static_assert(is_borrowed_ref_v<R_int>);
static_assert(is_borrowed_ref_v<R_holder>);
static_assert(is_borrowed_ref_v<R_double>);
static_assert(is_borrowed_ref_v<R_const>);

static_assert(is_borrowed_ref_v<R_int&>);
static_assert(is_borrowed_ref_v<R_int const&>);
static_assert(is_borrowed_ref_v<R_int&&>);

static_assert(!is_borrowed_ref_v<int>);
static_assert(!is_borrowed_ref_v<int*>,
              "A bare T* must not satisfy is_borrowed_ref_v. It is the underlying carrier, not "
              "the wrapper. Without this rejection, raw pointers slip through concept gates that "
              "expect a BorrowedRef.");
static_assert(!is_borrowed_ref_v<int&>);
static_assert(!is_borrowed_ref_v<R_int*>);
static_assert(!is_borrowed_ref_v<void>);

struct LookalikeBorrowedRef {
    int* ptr_;
};
static_assert(!is_borrowed_ref_v<LookalikeBorrowedRef>,
              "is_borrowed_ref_v must reject lookalikes. If this fires, the partial "
              "specialization has weakened to duck-typing and any struct holding a T* would "
              "falsely match.");

static_assert(IsBorrowedRef<R_int>);
static_assert(!IsBorrowedRef<int*>);

static_assert(std::is_same_v<borrowed_ref_value_t<R_int>, int>);
static_assert(std::is_same_v<borrowed_ref_value_t<R_holder>, Holder>);
static_assert(std::is_same_v<borrowed_ref_value_t<R_const>, const int>);

inline void runtime_smoke_test() {
    if (!is_borrowed_ref_v<R_int>) std::abort();
    if (is_borrowed_ref_v<int*>) std::abort();
    if (!IsBorrowedRef<R_holder>) std::abort();
    if (IsBorrowedRef<int>) std::abort();
    if (!std::is_same_v<borrowed_ref_value_t<R_double>, double>) std::abort();
}

}  // namespace detail::is_borrowed_ref_self_test

}  // namespace crucible::safety::extract
