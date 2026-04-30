#pragma once

// FOUND-D26 — wrapper-detection predicate for `Wait<Strategy, T>`.

#include <crucible/safety/Wait.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::WaitStrategy_v;

namespace detail {

template <typename T>
struct is_wait_impl : std::false_type {
    using value_type = void;
};

template <WaitStrategy_v Strategy, typename U>
struct is_wait_impl<::crucible::safety::Wait<Strategy, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr WaitStrategy_v strategy = Strategy;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_wait_v =
    detail::is_wait_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsWait = is_wait_v<T>;

template <typename T>
    requires is_wait_v<T>
using wait_value_t =
    typename detail::is_wait_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_wait_v<T>
inline constexpr WaitStrategy_v wait_strategy_v =
    detail::is_wait_impl<std::remove_cvref_t<T>>::strategy;

namespace detail::is_wait_self_test {

using W_int_spin    = ::crucible::safety::Wait<WaitStrategy_v::SpinPause,   int>;
using W_int_block   = ::crucible::safety::Wait<WaitStrategy_v::Block,       int>;
using W_double_park = ::crucible::safety::Wait<WaitStrategy_v::Park,        double>;

static_assert(is_wait_v<W_int_spin>);
static_assert(is_wait_v<W_int_block>);
static_assert(is_wait_v<W_double_park>);
static_assert(is_wait_v<W_int_spin&>);
static_assert(is_wait_v<W_int_spin const&>);

static_assert(!is_wait_v<int>);
static_assert(!is_wait_v<int*>);
static_assert(!is_wait_v<W_int_spin*>);

struct LookalikeWait { int v; WaitStrategy_v s; };
static_assert(!is_wait_v<LookalikeWait>);

static_assert(IsWait<W_int_spin>);
static_assert(!IsWait<int>);

static_assert(std::is_same_v<wait_value_t<W_int_spin>, int>);
static_assert(std::is_same_v<wait_value_t<W_double_park>, double>);
static_assert(wait_strategy_v<W_int_spin>  == WaitStrategy_v::SpinPause);
static_assert(wait_strategy_v<W_int_block> == WaitStrategy_v::Block);

}  // namespace detail::is_wait_self_test

inline bool is_wait_smoke_test() noexcept {
    using namespace detail::is_wait_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_wait_v<W_int_spin>;
        ok = ok && !is_wait_v<int>;
        ok = ok && IsWait<W_int_spin&&>;
        ok = ok && (wait_strategy_v<W_int_block> == WaitStrategy_v::Block);
    }
    return ok;
}

}  // namespace crucible::safety::extract
