#pragma once

// Recognizes a function that holds a producer-side channel handle and
// pushes one region of payload into it.

#include <crucible/safety/_IsOwnedRegion.h>
#include <crucible/safety/IsProducerHandle.h>
#include <crucible/safety/_SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

template <auto FnPtr>
concept ProducerEndpoint =
    arity_v<FnPtr> == 2 && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>> && is_producer_handle_v<param_type_t<FnPtr, 0>>
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 1>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 1>>> && is_owned_region_v<param_type_t<FnPtr, 1>>
    && std::is_void_v<return_type_t<FnPtr>>;

template <auto FnPtr>
inline constexpr bool is_producer_endpoint_v = ProducerEndpoint<FnPtr>;

template <auto FnPtr>
    requires ProducerEndpoint<FnPtr>
using producer_endpoint_handle_value_t = producer_handle_value_t<param_type_t<FnPtr, 0>>;

template <auto FnPtr>
    requires ProducerEndpoint<FnPtr>
using producer_endpoint_region_tag_t = owned_region_tag_t<param_type_t<FnPtr, 1>>;

template <auto FnPtr>
    requires ProducerEndpoint<FnPtr>
using producer_endpoint_region_value_t = owned_region_value_t<param_type_t<FnPtr, 1>>;

// A well-formed dispatch needs the handle's payload type and the
// region's element type to agree, since one is pushed into the other.
// The concept above deliberately does not demand it.  Admitting the
// syntactic match lets the mismatch be reported as what it is, rather
// than as a shape that was not recognized.
template <auto FnPtr>
    requires ProducerEndpoint<FnPtr>
inline constexpr bool producer_endpoint_value_consistent_v =
    std::is_same_v<producer_endpoint_handle_value_t<FnPtr>, producer_endpoint_region_value_t<FnPtr>>;

// Only the negatives are covered here, because a positive would need
// a region and a handle instantiated in every consumer of this header.

namespace detail::producer_endpoint_self_test {

inline void f_nullary() noexcept {}
static_assert(!ProducerEndpoint<&f_nullary>);

inline void f_one_int(int) noexcept {}
static_assert(!ProducerEndpoint<&f_one_int>);

inline void f_two_ints(int, int) noexcept {}
static_assert(!ProducerEndpoint<&f_two_ints>);

inline void f_three_params(int, int, int) noexcept {}
static_assert(!ProducerEndpoint<&f_three_params>);

}  // namespace detail::producer_endpoint_self_test

inline bool producer_endpoint_smoke_test() noexcept {
    using namespace detail::producer_endpoint_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !ProducerEndpoint<&f_nullary>;
        ok = ok && !ProducerEndpoint<&f_one_int>;
        ok = ok && !ProducerEndpoint<&f_two_ints>;
        ok = ok && !ProducerEndpoint<&f_three_params>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
