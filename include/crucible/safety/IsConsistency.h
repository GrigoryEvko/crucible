#pragma once

#include <crucible/safety/Consistency.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::Consistency_v;

namespace detail {

template <typename T>
struct is_consistency_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_level = false;
};

template <Consistency_v Level, typename U>
struct is_consistency_impl<::crucible::safety::Consistency<Level, U>> : std::true_type {
    using value_type = U;
    static constexpr Consistency_v level = Level;
    static constexpr bool has_level = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_consistency_v = detail::is_consistency_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsConsistency = is_consistency_v<T>;

template <typename T>
    requires is_consistency_v<T>
using consistency_value_t = typename detail::is_consistency_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_consistency_v<T>
inline constexpr Consistency_v consistency_level_v = detail::is_consistency_impl<std::remove_cvref_t<T>>::level;

namespace detail::is_consistency_self_test {

using C_int_strong = ::crucible::safety::Consistency<Consistency_v::STRONG, int>;
using C_double_eventual = ::crucible::safety::Consistency<Consistency_v::EVENTUAL, double>;
using C_int_causal = ::crucible::safety::Consistency<Consistency_v::CAUSAL_PREFIX, int>;

static_assert(is_consistency_v<C_int_strong>);
static_assert(is_consistency_v<C_double_eventual>);
static_assert(is_consistency_v<C_int_causal>);

static_assert(is_consistency_v<C_int_strong&>);
static_assert(is_consistency_v<C_int_strong&&>);
static_assert(is_consistency_v<C_int_strong const&>);
static_assert(is_consistency_v<C_int_strong const>);
static_assert(is_consistency_v<C_int_strong const&&>);

static_assert(!is_consistency_v<int>);
static_assert(!is_consistency_v<int*>);
static_assert(!is_consistency_v<int&>);
static_assert(!is_consistency_v<void>);

struct LookalikeConsistency {
    int value;
    Consistency_v level;
};
static_assert(!is_consistency_v<LookalikeConsistency>);

static_assert(!is_consistency_v<C_int_strong*>);

static_assert(IsConsistency<C_int_strong>);
static_assert(IsConsistency<C_int_strong&&>);
static_assert(!IsConsistency<int>);

static_assert(std::is_same_v<consistency_value_t<C_int_strong>, int>);
static_assert(std::is_same_v<consistency_value_t<C_double_eventual>, double>);

static_assert(std::is_same_v<consistency_value_t<C_int_strong const&>, int>);
static_assert(std::is_same_v<consistency_value_t<C_int_strong&&>, int>);

static_assert(consistency_level_v<C_int_strong> == Consistency_v::STRONG);
static_assert(consistency_level_v<C_double_eventual> == Consistency_v::EVENTUAL);
static_assert(consistency_level_v<C_int_causal> == Consistency_v::CAUSAL_PREFIX);
static_assert(consistency_level_v<C_int_strong const&> == Consistency_v::STRONG);

static_assert(std::is_same_v<consistency_value_t<C_int_strong>, consistency_value_t<C_int_causal>>);
static_assert(consistency_level_v<C_int_strong> != consistency_level_v<C_int_causal>);

}  // namespace detail::is_consistency_self_test

// The volatile bound defeats constant folding, so the predicate and the
// concept are evaluated outside a constant expression as well.
inline bool is_consistency_smoke_test() noexcept {
    using namespace detail::is_consistency_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_consistency_v<C_int_strong>;
        ok = ok && !is_consistency_v<int>;
        ok = ok && IsConsistency<C_int_strong&&>;
        ok = ok && (consistency_level_v<C_int_strong> == Consistency_v::STRONG);
    }
    return ok;
}

}  // namespace crucible::safety::extract
