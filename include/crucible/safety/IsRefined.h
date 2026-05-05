#pragma once

// Wrapper-detection predicate for safety::Refined<Pred, T> and
// safety::SealedRefined<Pred, T>.
//
// Refined and SealedRefined share the same predicate/value substrate but
// expose different mutation/extraction surfaces.  The generic IsRefined
// predicate intentionally admits both; refined_is_sealed_v<T> preserves the
// distinction for call sites that need to branch on the public API surface.

#include <crucible/safety/SealedRefined.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_refined_impl : std::false_type {
    using value_type = void;
    using predicate_type = void;
    static constexpr bool sealed = false;
};

template <auto Pred, typename T>
struct is_refined_impl<::crucible::safety::Refined<Pred, T>>
    : std::true_type
{
    using value_type = T;
    using predicate_type = decltype(Pred);
    static constexpr bool sealed = false;
};

template <auto Pred, typename T>
struct is_refined_impl<::crucible::safety::SealedRefined<Pred, T>>
    : std::true_type
{
    using value_type = T;
    using predicate_type = decltype(Pred);
    static constexpr bool sealed = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_refined_v =
    detail::is_refined_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsRefined = is_refined_v<T>;

template <typename T>
    requires is_refined_v<T>
using refined_value_t =
    typename detail::is_refined_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_refined_v<T>
using refined_predicate_type_t =
    typename detail::is_refined_impl<std::remove_cvref_t<T>>::predicate_type;

template <typename T>
    requires is_refined_v<T>
inline constexpr bool refined_is_sealed_v =
    detail::is_refined_impl<std::remove_cvref_t<T>>::sealed;

namespace detail::is_refined_self_test {

using R_pos_int = ::crucible::safety::Refined<
    ::crucible::safety::positive, int>;
using R_nonneg_int = ::crucible::safety::Refined<
    ::crucible::safety::non_negative, int>;
using SR_pos_int = ::crucible::safety::SealedRefined<
    ::crucible::safety::positive, int>;

struct LookalikeRefined {
    using value_type = int;
    using predicate_type = decltype(::crucible::safety::positive);
};

static_assert( is_refined_v<R_pos_int>);
static_assert( is_refined_v<R_nonneg_int>);
static_assert( is_refined_v<SR_pos_int>);

static_assert( is_refined_v<R_pos_int&>);
static_assert( is_refined_v<R_pos_int&&>);
static_assert( is_refined_v<R_pos_int const>);
static_assert( is_refined_v<R_pos_int const&>);
static_assert( is_refined_v<R_pos_int volatile>);

static_assert(!is_refined_v<int>);
static_assert(!is_refined_v<int*>);
static_assert(!is_refined_v<R_pos_int*>);
static_assert(!is_refined_v<void>);
static_assert(!is_refined_v<LookalikeRefined>);

static_assert( IsRefined<R_pos_int>);
static_assert( IsRefined<SR_pos_int const&>);
static_assert(!IsRefined<int>);

static_assert(std::is_same_v<refined_value_t<R_pos_int>, int>);
static_assert(std::is_same_v<refined_value_t<SR_pos_int const&>, int>);
static_assert(std::is_same_v<
    refined_predicate_type_t<R_pos_int>,
    std::remove_cv_t<decltype(::crucible::safety::positive)>>);
static_assert(!refined_is_sealed_v<R_pos_int>);
static_assert( refined_is_sealed_v<SR_pos_int>);

inline bool runtime_smoke_test() noexcept {
    volatile int const cap = 4;
    bool ok = true;
    for (int i = 0; i < cap; ++i) {
        ok = ok && is_refined_v<R_pos_int>;
        ok = ok && is_refined_v<SR_pos_int const&>;
        ok = ok && !is_refined_v<int>;
        ok = ok && !is_refined_v<LookalikeRefined>;
        ok = ok && refined_is_sealed_v<SR_pos_int>;
    }
    return ok;
}

}  // namespace detail::is_refined_self_test

}  // namespace crucible::safety::extract
