// The compile-time checks of fixy/Saturate.h.

#include <fixy/Saturate.h>

namespace fixy::sat {

namespace detail::saturate_self_test {

// The shapes as shipped.  A det result is the pure band over the checked
// result and costs nothing beyond it; a from result is the checked
// result itself.  The values these return under a sequence of calls are
// in test/fixy/test_saturate.cpp, and the four refusals — a const
// destination, a raw escape from a from result, a raw escape from a det
// result, and a cross-tier assignment — are test/fixy/neg/neg_sat_*.
static_assert(std::is_same_v<decltype(add_sat_det<std::uint32_t>(1u, 2u)), DetSatPure<std::uint32_t>>);
static_assert(std::is_same_v<decltype(sub_sat_det<std::int8_t>(1, 2)), DetSatPure<std::int8_t>>);
static_assert(std::is_same_v<decltype(mul_sat_det<std::uint64_t>(1u, 2u)), DetSatPure<std::uint64_t>>);
static_assert(sizeof(DetSatPure<std::uint64_t>) == sizeof(Saturated<std::uint64_t>));
static_assert(
    std::is_same_v<decltype(add_sat_from(std::declval<std::uint32_t const&>(), 1u)), Saturated<std::uint32_t>>);
static_assert(std::is_same_v<decltype(add_sat_into(std::declval<std::uint32_t&>(), 1u)), Saturated<std::uint32_t>>);
static_assert(std::is_same_v<decltype(add_sat<std::uint32_t>(1u, 2u)), std::uint32_t>);

}  // namespace detail::saturate_self_test

}  // namespace fixy::sat
