// The compile-time checks of fixy/fp/RoundToFloat.h.

#include <fixy/fp/RoundToFloat.h>

#include <foundation/core/Scalar.h>

#include <cstdint>

namespace fixy::fp::detail::round_to_float_self_test {

[[nodiscard]] consteval double double_of(std::uint64_t bits) noexcept {
    return ::foundation::core::bit_cast<double>(bits);
}

// 1 + 2^-24 is the midpoint of 1 and the next float.  Its neighbors in double
// are no midpoint, and a float is no midpoint.
static_assert(is_float_midpoint_(1.0 + 0x1p-24), "1 + 2^-24 is a float midpoint.");
static_assert(!is_float_midpoint_(1.0 + 0x1p-24 + 0x1p-52), "the double above the midpoint is no midpoint.");
static_assert(!is_float_midpoint_(1.0 + 0x1p-24 - 0x1p-52), "the double below the midpoint is no midpoint.");
static_assert(!is_float_midpoint_(1.0), "a float is no midpoint.");
static_assert(is_float_midpoint_(-(1.0 + 0x1p-24)), "the test reads the magnitude.");

// Below 1, the midpoint of 1 - 2^-24 and 1 is 1 - 2^-25.
static_assert(is_float_midpoint_(1.0 - 0x1p-25), "1 - 2^-25 is a float midpoint.");

// A subnormal float: the midpoint of 0 and the smallest subnormal float, and
// of the two smallest subnormal floats.
static_assert(is_float_midpoint_(0x1p-150), "2^-150 is the midpoint of 0 and 2^-149.");
static_assert(is_float_midpoint_(0x1.8p-149), "1.5 * 2^-149 is the midpoint of 2^-149 and 2^-148.");
static_assert(!is_float_midpoint_(0x1.4p-149), "1.25 * 2^-149 is no midpoint.");

// The rounding test: the midpoint and its neighbors within the tolerance, and
// a value farther than the tolerance.
static_assert(is_near_float_midpoint_(1.0 + 0x1p-24, 4), "the midpoint is near a midpoint.");
static_assert(is_near_float_midpoint_(1.0 + 0x1p-24 + 4 * 0x1p-52, 4), "four units above is near.");
static_assert(is_near_float_midpoint_(1.0 + 0x1p-24 - 4 * 0x1p-52, 4), "four units below is near.");
static_assert(!is_near_float_midpoint_(1.0 + 0x1p-24 + 5 * 0x1p-52, 4), "five units above is not near.");
static_assert(!is_near_float_midpoint_(1.0 + 0x1p-24 - 5 * 0x1p-52, 4), "five units below is not near.");
static_assert(!is_near_float_midpoint_(double_of(0x3FF0000000000000ULL), 4), "a float is not near a midpoint.");

}  // namespace fixy::fp::detail::round_to_float_self_test
