#pragma once

#include <crucible/safety/reduce_into.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_reduce_into_impl : std::false_type {
    using accumulator_type = void;
    using reducer_type = void;
};

template <typename R, typename Op>
struct is_reduce_into_impl<::crucible::safety::reduce_into<R, Op>> : std::true_type {
    using accumulator_type = R;
    using reducer_type = Op;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_reduce_into_v = detail::is_reduce_into_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsReduceInto = is_reduce_into_v<T>;

// The two extractors are constrained rather than left to the primary
// template, which would hand back void for an unrelated argument instead of
// failing.

template <typename T>
    requires is_reduce_into_v<T>
using reduce_into_accumulator_t = typename detail::is_reduce_into_impl<std::remove_cvref_t<T>>::accumulator_type;

template <typename T>
    requires is_reduce_into_v<T>
using reduce_into_reducer_t = typename detail::is_reduce_into_impl<std::remove_cvref_t<T>>::reducer_type;

namespace detail::is_reduce_into_self_test {

struct PlusOp {
    constexpr int operator()(int const& a, int const& b) const noexcept { return a + b; }
};

struct DoublePlusOp {
    constexpr double operator()(double const& a, double const& b) const noexcept { return a + b; }
};

using RI_int_plus = ::crucible::safety::reduce_into<int, PlusOp>;
using RI_double_plus = ::crucible::safety::reduce_into<double, DoublePlusOp>;

static_assert(is_reduce_into_v<RI_int_plus>);
static_assert(is_reduce_into_v<RI_double_plus>);

static_assert(is_reduce_into_v<RI_int_plus&>);
static_assert(is_reduce_into_v<RI_int_plus&&>);
static_assert(is_reduce_into_v<RI_int_plus const&>);
static_assert(is_reduce_into_v<RI_int_plus const>);
static_assert(is_reduce_into_v<RI_int_plus const&&>);

static_assert(!is_reduce_into_v<int>);
static_assert(!is_reduce_into_v<int*>);
static_assert(!is_reduce_into_v<int&>);
static_assert(!is_reduce_into_v<void>);
static_assert(!is_reduce_into_v<PlusOp>);

struct LookalikeReduceInto {
    int acc;
    PlusOp op;
};
static_assert(!is_reduce_into_v<LookalikeReduceInto>);

static_assert(IsReduceInto<RI_int_plus>);
static_assert(IsReduceInto<RI_int_plus&&>);
static_assert(!IsReduceInto<int>);
static_assert(!IsReduceInto<PlusOp>);

static_assert(std::is_same_v<reduce_into_accumulator_t<RI_int_plus>, int>);
static_assert(std::is_same_v<reduce_into_accumulator_t<RI_double_plus>, double>);
static_assert(std::is_same_v<reduce_into_reducer_t<RI_int_plus>, PlusOp>);
static_assert(std::is_same_v<reduce_into_reducer_t<RI_double_plus>, DoublePlusOp>);

static_assert(std::is_same_v<reduce_into_accumulator_t<RI_int_plus const&>, int>);
static_assert(std::is_same_v<reduce_into_reducer_t<RI_int_plus&&>, PlusOp>);

static_assert(!std::is_same_v<reduce_into_accumulator_t<RI_int_plus>, reduce_into_accumulator_t<RI_double_plus>>);
static_assert(!std::is_same_v<reduce_into_reducer_t<RI_int_plus>, reduce_into_reducer_t<RI_double_plus>>);

}  // namespace detail::is_reduce_into_self_test

inline bool is_reduce_into_smoke_test() noexcept {
    using namespace detail::is_reduce_into_self_test;

    // The volatile bound defeats constant folding, so the trait reads survive
    // dead-code elimination.
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_reduce_into_v<RI_int_plus>;
        ok = ok && !is_reduce_into_v<int>;
        ok = ok && IsReduceInto<RI_int_plus&&>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
