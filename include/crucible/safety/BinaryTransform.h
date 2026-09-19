#pragma once

#include <crucible/safety/SignatureTraits.h>
#include <crucible/safety/_IsOwnedRegion.h>

#include <type_traits>

namespace crucible::safety::extract {

// Both parameters are consumed, so each must be a non-const rvalue reference: a
// const rvalue cannot be moved from.  is_owned_region_v strips cv and reference
// qualifiers, so the reference-category checks are separate clauses rather than
// folded into it.  The two regions may share a tag or carry different ones.
template <auto FnPtr>
concept BinaryTransform =
    arity_v<FnPtr> == 2 && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>> && is_owned_region_v<param_type_t<FnPtr, 0>>
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 1>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 1>>> && is_owned_region_v<param_type_t<FnPtr, 1>>
    && (std::is_void_v<return_type_t<FnPtr>> || is_owned_region_v<return_type_t<FnPtr>>);

template <auto FnPtr>
inline constexpr bool is_binary_transform_v = BinaryTransform<FnPtr>;

template <auto FnPtr>
inline constexpr bool is_in_place_binary_transform_v = BinaryTransform<FnPtr> && std::is_void_v<return_type_t<FnPtr>>;

template <auto FnPtr>
    requires BinaryTransform<FnPtr>
using binary_transform_lhs_tag_t = owned_region_tag_t<param_type_t<FnPtr, 0>>;

template <auto FnPtr>
    requires BinaryTransform<FnPtr>
using binary_transform_rhs_tag_t = owned_region_tag_t<param_type_t<FnPtr, 1>>;

template <auto FnPtr>
    requires BinaryTransform<FnPtr>
using binary_transform_lhs_value_t = owned_region_value_t<param_type_t<FnPtr, 0>>;

template <auto FnPtr>
    requires BinaryTransform<FnPtr>
using binary_transform_rhs_value_t = owned_region_value_t<param_type_t<FnPtr, 1>>;

// A class-template specialization rather than a conditional alias: a
// conditional would instantiate owned_region_tag_t on the void return type
// even when that branch is not selected.
namespace detail {

template <auto FnPtr, bool IsInPlace>
struct binary_transform_output_tag_select;

template <auto FnPtr>
struct binary_transform_output_tag_select<FnPtr, true> {
    using type = void;
};

template <auto FnPtr>
struct binary_transform_output_tag_select<FnPtr, false> {
    using type = owned_region_tag_t<return_type_t<FnPtr>>;
};

}  // namespace detail

template <auto FnPtr>
    requires BinaryTransform<FnPtr>
using binary_transform_output_tag_t =
    typename detail::binary_transform_output_tag_select<FnPtr, std::is_void_v<return_type_t<FnPtr>>>::type;

template <auto FnPtr>
    requires BinaryTransform<FnPtr>
inline constexpr bool binary_transform_has_same_tag_v =
    std::is_same_v<binary_transform_lhs_tag_t<FnPtr>, binary_transform_rhs_tag_t<FnPtr>>;

// The in-header cases are negative only.  A positive case needs a concrete
// region specialization, which would widen this header's include surface.

namespace detail::binary_transform_self_test {

inline void f_nullary() noexcept {}
static_assert(!BinaryTransform<&f_nullary>);

inline void f_one_int(int) noexcept {}
static_assert(!BinaryTransform<&f_one_int>);

inline void f_two_ints(int, int) noexcept {}
static_assert(!BinaryTransform<&f_two_ints>);

inline void f_three_params(int, int, int) noexcept {}
static_assert(!BinaryTransform<&f_three_params>);

}  // namespace detail::binary_transform_self_test

inline bool binary_transform_smoke_test() noexcept {
    using namespace detail::binary_transform_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !BinaryTransform<&f_nullary>;
        ok = ok && !BinaryTransform<&f_one_int>;
        ok = ok && !BinaryTransform<&f_two_ints>;
        ok = ok && !BinaryTransform<&f_three_params>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
