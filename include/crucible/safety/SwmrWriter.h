#pragma once

// Recognizes a function that holds a single-writer channel handle and
// publishes one value into it.  A publish carries a single value and
// not a region, so the value parameter is taken by value.
//
// The predicate here is spelled with a `_function` suffix because the
// handle-side predicate of the same name, which this file also uses,
// takes a type where this one takes a function.

#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsSwmrHandle.h>
#include <crucible/safety/SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

// The region rejection on the second parameter is what keeps this
// shape disjoint from the producer-endpoint shape, which is otherwise
// written the same way.  A pointer is still admitted: a pointer passed
// by value is a value, and it is published as one.
template <auto FnPtr>
concept SwmrWriter =
    arity_v<FnPtr> == 2 && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>>
    && is_swmr_writer_v<std::remove_cvref_t<param_type_t<FnPtr, 0>>> && !std::is_reference_v<param_type_t<FnPtr, 1>>
    && !is_owned_region_v<param_type_t<FnPtr, 1>> && std::is_void_v<return_type_t<FnPtr>>;

template <auto FnPtr>
inline constexpr bool is_swmr_writer_function_v = SwmrWriter<FnPtr>;

template <auto FnPtr>
    requires SwmrWriter<FnPtr>
using swmr_writer_handle_value_t = swmr_writer_value_t<std::remove_cvref_t<param_type_t<FnPtr, 0>>>;

template <auto FnPtr>
    requires SwmrWriter<FnPtr>
using swmr_writer_published_value_t = std::remove_cv_t<param_type_t<FnPtr, 1>>;

// The two types must match exactly, or the publish call converts.  The
// concept above deliberately admits the mismatch so it can be reported
// as what it is, rather than as a shape that was not recognized.
template <auto FnPtr>
    requires SwmrWriter<FnPtr>
inline constexpr bool swmr_writer_value_consistent_v =
    std::is_same_v<swmr_writer_handle_value_t<FnPtr>, swmr_writer_published_value_t<FnPtr>>;

namespace detail::swmr_writer_self_test {

inline void f_nullary() noexcept {}
static_assert(!SwmrWriter<&f_nullary>);

inline void f_one_int(int) noexcept {}
static_assert(!SwmrWriter<&f_one_int>);

inline void f_two_ints(int, int) noexcept {}
static_assert(!SwmrWriter<&f_two_ints>);

inline void f_three_params(int, int, int) noexcept {}
static_assert(!SwmrWriter<&f_three_params>);

}  // namespace detail::swmr_writer_self_test

inline bool swmr_writer_smoke_test() noexcept {
    using namespace detail::swmr_writer_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !SwmrWriter<&f_nullary>;
        ok = ok && !SwmrWriter<&f_one_int>;
        ok = ok && !SwmrWriter<&f_two_ints>;
        ok = ok && !SwmrWriter<&f_three_params>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
