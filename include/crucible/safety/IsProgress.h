#pragma once

// FOUND-D28 — wrapper-detection predicate for `Progress<Class, T>`.

#include <crucible/safety/Progress.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::ProgressClass_v;

namespace detail {

template <typename T>
struct is_progress_impl : std::false_type {
    using value_type = void;
};

template <ProgressClass_v Class, typename U>
struct is_progress_impl<::crucible::safety::Progress<Class, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr ProgressClass_v progress_class = Class;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_progress_v =
    detail::is_progress_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsProgress = is_progress_v<T>;

template <typename T>
    requires is_progress_v<T>
using progress_value_t =
    typename detail::is_progress_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_progress_v<T>
inline constexpr ProgressClass_v progress_class_v =
    detail::is_progress_impl<std::remove_cvref_t<T>>::progress_class;

namespace detail::is_progress_self_test {

using P_int_bnd  = ::crucible::safety::Progress<ProgressClass_v::Bounded,    int>;
using P_int_prod = ::crucible::safety::Progress<ProgressClass_v::Productive, int>;
using P_int_term = ::crucible::safety::Progress<ProgressClass_v::Terminating, int>;
using P_int_div  = ::crucible::safety::Progress<ProgressClass_v::MayDiverge, int>;
using P_dbl_bnd  = ::crucible::safety::Progress<ProgressClass_v::Bounded,    double>;

static_assert(is_progress_v<P_int_bnd>);
static_assert(is_progress_v<P_int_prod>);
static_assert(is_progress_v<P_int_term>);
static_assert(is_progress_v<P_int_div>);
static_assert(is_progress_v<P_dbl_bnd>);
static_assert(is_progress_v<P_int_bnd&>);
static_assert(is_progress_v<P_int_bnd const&>);

static_assert(!is_progress_v<int>);
static_assert(!is_progress_v<int*>);
static_assert(!is_progress_v<P_int_bnd*>);

struct LookalikeProgress { int v; ProgressClass_v c; };
static_assert(!is_progress_v<LookalikeProgress>);

static_assert(IsProgress<P_int_bnd>);
static_assert(!IsProgress<int>);

static_assert(std::is_same_v<progress_value_t<P_int_bnd>, int>);
static_assert(std::is_same_v<progress_value_t<P_dbl_bnd>, double>);
static_assert(progress_class_v<P_int_bnd>  == ProgressClass_v::Bounded);
static_assert(progress_class_v<P_int_prod> == ProgressClass_v::Productive);
static_assert(progress_class_v<P_int_div>  == ProgressClass_v::MayDiverge);

}  // namespace detail::is_progress_self_test

inline bool is_progress_smoke_test() noexcept {
    using namespace detail::is_progress_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_progress_v<P_int_bnd>;
        ok = ok && !is_progress_v<int>;
        ok = ok && IsProgress<P_int_bnd&&>;
        ok = ok && (progress_class_v<P_int_div> == ProgressClass_v::MayDiverge);
    }
    return ok;
}

}  // namespace crucible::safety::extract
