#pragma once

// The round of a double result to a float, for the float forms of
// fixy/fp/Exp.h and Log.h, and for the oracle of test_fp_erf_float_exhaustive.
// Each float form computes in double and rounds one time.  The helpers here
// decide when that round is safe.
//
// Rounding test of a fast path
//   A fast path gives a double y with a relative error of at most e.  The
//   round of y to a float is the correctly rounded result unless a float
//   midpoint (the mean of two neighbor floats) lies within e * |y| of y.  For a
//   float result of 24 bits, the 29 bits of y below the float precision tell
//   the distance to the nearest midpoint: the midpoint is at 2^28 of those
//   bits.  is_near_float_midpoint_ is true when the distance is at most the
//   tolerance, in units of the last bit of y.  A tolerance of e * 2^53 covers
//   each y of the binade.  The test holds only for a result of 24 bits: a
//   caller sends each subnormal result and each overflow to its slow path
//   before the test.
//
// Midpoint test of a slow path
//   A slow path gives a double y with an error below one unit in the last
//   place of a double.  If the round of y to a float is wrong, a float
//   midpoint lies between y and the true value.  A float midpoint is a double,
//   and no other double is nearer y than one unit in the last place.  So y is
//   that midpoint.  is_float_midpoint_ finds exactly that case, also for a
//   subnormal float result.  In that case the caller gives the result from a
//   list of inputs that the sweep of the oracle found.
//
// Each operation is an integer operation on the bits, a conversion between
// float and double, an add or a multiply by one half, so each step is exact or
// is the one round of the result.

#include <cstdint>

namespace fixy::fp::detail {

// The bits of a double below the precision of a float: 52 - 23.
inline constexpr std::uint64_t kBelowFloatMask = (std::uint64_t{1} << 29) - 1;
inline constexpr std::uint64_t kFloatMidpointBits = std::uint64_t{1} << 28;

// True when y lies within tolerance units of its last bit of a float
// midpoint.  y must round to a normal finite float.
[[nodiscard]] constexpr bool is_near_float_midpoint_(double result, std::uint64_t tolerance) noexcept {
    std::uint64_t const offset =
        (__builtin_bit_cast(std::uint64_t, result) - (kFloatMidpointBits - tolerance)) & kBelowFloatMask;
    return offset <= 2 * tolerance;
}

// True when y is exactly the mean of two neighbor floats, at the precision of
// the float that y rounds to (a subnormal float too).  y is finite.
[[nodiscard]] constexpr bool is_float_midpoint_(double result) noexcept {
    float const nearest = static_cast<float>(result);
    double const widened = static_cast<double>(nearest);
    std::uint64_t const result_bits = __builtin_bit_cast(std::uint64_t, result);
    std::uint64_t const widened_bits = __builtin_bit_cast(std::uint64_t, widened);
    if (result_bits == widened_bits) return false;
    std::uint64_t constexpr magnitude_mask = ~std::uint64_t{0} >> 1;
    // The neighbor float on the side of y.  A result of a larger magnitude
    // than its float has the neighbor of the next larger magnitude.
    std::uint32_t const nearest_bits = __builtin_bit_cast(std::uint32_t, nearest);
    bool const is_beyond = (result_bits & magnitude_mask) > (widened_bits & magnitude_mask);
    std::uint32_t const neighbor_bits = is_beyond ? nearest_bits + 1u : nearest_bits - 1u;
    double const neighbor = static_cast<double>(__builtin_bit_cast(float, neighbor_bits));
    double const midpoint = (widened + neighbor) * 0.5;
    return __builtin_bit_cast(std::uint64_t, midpoint) == result_bits;
}

}  // namespace fixy::fp::detail
