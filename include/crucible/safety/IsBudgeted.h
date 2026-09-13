#pragma once

#include <crucible/safety/Budgeted.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_budgeted_impl : std::false_type {
    using value_type = void;
};

template <typename U>
struct is_budgeted_impl<::crucible::safety::Budgeted<U>> : std::true_type {
    using value_type = U;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_budgeted_v = detail::is_budgeted_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsBudgeted = is_budgeted_v<T>;

template <typename T>
    requires is_budgeted_v<T>
using budgeted_value_t = typename detail::is_budgeted_impl<std::remove_cvref_t<T>>::value_type;

namespace detail::is_budgeted_self_test {

using B_int = ::crucible::safety::Budgeted<int>;
using B_double = ::crucible::safety::Budgeted<double>;
using B_char = ::crucible::safety::Budgeted<char>;
using B_uint64 = ::crucible::safety::Budgeted<std::uint64_t>;

static_assert(is_budgeted_v<B_int>);
static_assert(is_budgeted_v<B_double>);
static_assert(is_budgeted_v<B_char>);
static_assert(is_budgeted_v<B_uint64>);

static_assert(is_budgeted_v<B_int&>);
static_assert(is_budgeted_v<B_int const&>);

static_assert(!is_budgeted_v<int>);
static_assert(!is_budgeted_v<int*>);
static_assert(!is_budgeted_v<void>);

struct LookalikeBudgeted {
    int value;
    std::uint64_t bits_field;
    std::uint64_t peak_field;
};
static_assert(!is_budgeted_v<LookalikeBudgeted>);

static_assert(!is_budgeted_v<B_int*>);

static_assert(IsBudgeted<B_int>);
static_assert(!IsBudgeted<int>);

static_assert(std::is_same_v<budgeted_value_t<B_int>, int>);
static_assert(std::is_same_v<budgeted_value_t<B_double>, double>);
static_assert(std::is_same_v<budgeted_value_t<B_uint64>, std::uint64_t>);

static_assert(sizeof(B_int) >= sizeof(int) + 16);
static_assert(sizeof(B_double) >= sizeof(double) + 16);
static_assert(sizeof(B_uint64) == 24);

}  // namespace detail::is_budgeted_self_test

inline bool is_budgeted_smoke_test() noexcept {
    using namespace detail::is_budgeted_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_budgeted_v<B_int>;
        ok = ok && is_budgeted_v<B_double>;
        ok = ok && !is_budgeted_v<int>;
        ok = ok && IsBudgeted<B_int&&>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
