#pragma once

#include <crucible/safety/SignatureTraits.h>
#include <crucible/safety/IsOwnedRegion.h>

#include <type_traits>

namespace crucible::safety::extract {

// The shape consumes its region, so parameter 0 must be a non-const rvalue
// reference: a const rvalue cannot be moved from.  is_owned_region_v strips cv
// and reference qualifiers, so the reference-category checks are separate
// clauses rather than folded into it.
template <auto FnPtr>
concept UnaryTransform =
    arity_v<FnPtr> == 1 && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>> && is_owned_region_v<param_type_t<FnPtr, 0>>
    && (std::is_void_v<return_type_t<FnPtr>> || is_owned_region_v<return_type_t<FnPtr>>);

template <auto FnPtr>
inline constexpr bool is_unary_transform_v = UnaryTransform<FnPtr>;

template <auto FnPtr>
inline constexpr bool is_in_place_unary_transform_v = UnaryTransform<FnPtr> && std::is_void_v<return_type_t<FnPtr>>;

template <auto FnPtr>
    requires UnaryTransform<FnPtr>
using unary_transform_input_tag_t = owned_region_tag_t<param_type_t<FnPtr, 0>>;

template <auto FnPtr>
    requires UnaryTransform<FnPtr>
using unary_transform_input_value_t = owned_region_value_t<param_type_t<FnPtr, 0>>;

// A class-template specialization rather than a conditional alias: a
// conditional would instantiate owned_region_tag_t on the void return type
// even when that branch is not selected.
namespace detail {

template <auto FnPtr, bool IsInPlace>
struct unary_transform_output_tag_select;

template <auto FnPtr>
struct unary_transform_output_tag_select<FnPtr, true> {
    using type = void;
};

template <auto FnPtr>
struct unary_transform_output_tag_select<FnPtr, false> {
    using type = owned_region_tag_t<return_type_t<FnPtr>>;
};

}  // namespace detail

template <auto FnPtr>
    requires UnaryTransform<FnPtr>
using unary_transform_output_tag_t =
    typename detail::unary_transform_output_tag_select<FnPtr, std::is_void_v<return_type_t<FnPtr>>>::type;

// The in-header cases are negative only.  A positive case needs a concrete
// region specialization, which would widen this header's include surface.

namespace detail::unary_transform_self_test {

inline void f_nullary() noexcept {}
static_assert(!UnaryTransform<&f_nullary>);
static_assert(!is_unary_transform_v<&f_nullary>);

inline void f_int(int) noexcept {}
static_assert(!UnaryTransform<&f_int>);

inline void f_two_ints(int, int) noexcept {}
static_assert(!UnaryTransform<&f_two_ints>);

inline int f_returns_int(int) noexcept { return 0; }
static_assert(!UnaryTransform<&f_returns_int>);

}  // namespace detail::unary_transform_self_test

inline bool unary_transform_smoke_test() noexcept {
    using namespace detail::unary_transform_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !UnaryTransform<&f_nullary>;
        ok = ok && !UnaryTransform<&f_int>;
        ok = ok && !UnaryTransform<&f_two_ints>;
        ok = ok && !UnaryTransform<&f_returns_int>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
