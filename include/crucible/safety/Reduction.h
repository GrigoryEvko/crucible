#pragma once

// Recognizes a function that consumes one region and folds its
// elements into one borrowed accumulator.

#include <crucible/safety/_IsOwnedRegion.h>
#include <crucible/safety/IsReduceInto.h>
#include <crucible/safety/_SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

// Each qualifier on the accumulator earns its place.  It is an lvalue
// reference and not an rvalue one, because consuming it would defeat
// refining the same accumulator over several calls.  It is non-const,
// because a reducer that cannot mutate it makes no progress.  And the
// return is void, because the result is already in the accumulator.
template <auto FnPtr>
concept Reduction = arity_v<FnPtr> == 2 && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
                 && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>>
                 && is_owned_region_v<param_type_t<FnPtr, 0>> && std::is_lvalue_reference_v<param_type_t<FnPtr, 1>>
                 && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 1>>>
                 && is_reduce_into_v<param_type_t<FnPtr, 1>> && std::is_void_v<return_type_t<FnPtr>>;

template <auto FnPtr>
inline constexpr bool is_reduction_v = Reduction<FnPtr>;

template <auto FnPtr>
    requires Reduction<FnPtr>
using reduction_input_tag_t = owned_region_tag_t<param_type_t<FnPtr, 0>>;

template <auto FnPtr>
    requires Reduction<FnPtr>
using reduction_input_value_t = owned_region_value_t<param_type_t<FnPtr, 0>>;

template <auto FnPtr>
    requires Reduction<FnPtr>
using reduction_accumulator_t = reduce_into_accumulator_t<param_type_t<FnPtr, 1>>;

template <auto FnPtr>
    requires Reduction<FnPtr>
using reduction_reducer_t = reduce_into_reducer_t<param_type_t<FnPtr, 1>>;

// Only the negatives are covered here, because a positive would need
// a region and an accumulator instantiated in every consumer of this
// header.

namespace detail::reduction_self_test {

inline void f_nullary() noexcept {}
static_assert(!Reduction<&f_nullary>);
static_assert(!is_reduction_v<&f_nullary>);

inline void f_unary(int) noexcept {}
static_assert(!Reduction<&f_unary>);

inline void f_ternary(int, int, int) noexcept {}
static_assert(!Reduction<&f_ternary>);

inline void f_two_ints(int, int) noexcept {}
static_assert(!Reduction<&f_two_ints>);

inline int f_returns_int(int, int) noexcept { return 0; }
static_assert(!Reduction<&f_returns_int>);

}  // namespace detail::reduction_self_test

inline bool reduction_smoke_test() noexcept {
    using namespace detail::reduction_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !Reduction<&f_nullary>;
        ok = ok && !Reduction<&f_unary>;
        ok = ok && !Reduction<&f_ternary>;
        ok = ok && !Reduction<&f_two_ints>;
        ok = ok && !Reduction<&f_returns_int>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
