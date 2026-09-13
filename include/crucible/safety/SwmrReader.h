#pragma once

#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsSwmrHandle.h>
#include <crucible/safety/SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

// A reference return is rejected because the snapshot storage belongs to the
// publisher: a caller holding a reference into it observes a torn value if the
// writer publishes during the borrow.  The load must copy out.  A region return
// is rejected as a different shape, not because it is unsound.
template <auto FnPtr>
concept SwmrReader =
    arity_v<FnPtr> == 1 && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>>
    && is_swmr_reader_v<std::remove_cvref_t<param_type_t<FnPtr, 0>>> && !std::is_void_v<return_type_t<FnPtr>>
    && !std::is_reference_v<return_type_t<FnPtr>> && !is_owned_region_v<return_type_t<FnPtr>>;

template <auto FnPtr>
inline constexpr bool is_swmr_reader_function_v = SwmrReader<FnPtr>;

template <auto FnPtr>
    requires SwmrReader<FnPtr>
using swmr_reader_handle_value_t = swmr_reader_value_t<std::remove_cvref_t<param_type_t<FnPtr, 0>>>;

template <auto FnPtr>
    requires SwmrReader<FnPtr>
using swmr_reader_returned_value_t = std::remove_cv_t<return_type_t<FnPtr>>;

template <auto FnPtr>
    requires SwmrReader<FnPtr>
inline constexpr bool swmr_reader_value_consistent_v =
    std::is_same_v<swmr_reader_handle_value_t<FnPtr>, swmr_reader_returned_value_t<FnPtr>>;

namespace detail::swmr_reader_self_test {

inline void f_nullary() noexcept {}
static_assert(!SwmrReader<&f_nullary>);

inline void f_two_ints(int, int) noexcept {}
static_assert(!SwmrReader<&f_two_ints>);

inline int f_one_int(int) noexcept { return 0; }
static_assert(!SwmrReader<&f_one_int>);

inline void f_void_return_one_int(int) noexcept {}
static_assert(!SwmrReader<&f_void_return_one_int>);

}  // namespace detail::swmr_reader_self_test

inline bool swmr_reader_smoke_test() noexcept {
    using namespace detail::swmr_reader_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !SwmrReader<&f_nullary>;
        ok = ok && !SwmrReader<&f_two_ints>;
        ok = ok && !SwmrReader<&f_one_int>;
        ok = ok && !SwmrReader<&f_void_return_one_int>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
