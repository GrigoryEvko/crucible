// The compile-time checks of fixy/fp/DoubleWord.h.  A constant evaluation
// runs the arithmetic of GCC and the exact form of fma_of_finite, and
// test/fixy/test_fp_double_word.cpp runs the same operations at run time.

#include <fixy/fp/DoubleWord.h>

#include <foundation/core/Scalar.h>

#include <cstdint>

namespace fixy::fp::detail::double_word_self_test {

[[nodiscard]] consteval std::uint64_t bits_of(double value) noexcept {
    return ::foundation::core::bit_cast<std::uint64_t>(value);
}

// 1 + 2^-60 rounds to 1, and the error 2^-60 is the low part.
static_assert(bits_of(two_sum_(1.0, 0x1p-60).high) == bits_of(1.0), "two_sum_ rounds the sum to nearest.");
static_assert(bits_of(two_sum_(0x1p-60, 1.0).low) == bits_of(0x1p-60), "two_sum_ keeps the error in each order.");
static_assert(bits_of(fast_two_sum_(1.0, -0x1p-60).low) == bits_of(-0x1p-60),
              "fast_two_sum_ keeps the error for the larger value first.");
// (1 + 2^-52)^2 = 1 + 2^-51 + 2^-104, and the round drops 2^-104.
static_assert(bits_of(two_product_(1.0 + 0x1p-52, 1.0 + 0x1p-52).high) == bits_of(1.0 + 0x1p-51),
              "two_product_ rounds the product to nearest.");
static_assert(bits_of(two_product_(1.0 + 0x1p-52, 1.0 + 0x1p-52).low) == bits_of(0x1p-104),
              "two_product_ keeps the error of the product.");
// (1 + 2^-60) (1 + 2^-60) = 1 + 2^-59 + 2^-120, and the pair holds 1 + 2^-59.
static_assert(bits_of(dw_times_dw_(DoubleWord{1.0, 0x1p-60}, DoubleWord{1.0, 0x1p-60}).low) == bits_of(0x1p-59),
              "dw_times_dw_ keeps the cross terms in the low part.");
static_assert(bits_of(dw_plus_dw_(DoubleWord{1.0, 0x1p-60}, DoubleWord{-1.0, 0x1p-61}).high) == bits_of(0x1.8p-60),
              "dw_plus_dw_ is exact when the high parts cancel.");
// The ties of the round of three doubles at 1, where the gap below is half
// the gap above.
static_assert(bits_of(round_triple_(1.0, 0x1p-53, 0x1p-120)) == bits_of(1.0 + 0x1p-52),
              "round_triple_ rounds up past the midpoint above.");
static_assert(bits_of(round_triple_(1.0, 0x1p-53, -0x1p-120)) == bits_of(1.0),
              "round_triple_ rounds down below the midpoint above.");
static_assert(bits_of(round_triple_(1.0, -0x1p-54, -0x1p-120)) == bits_of(1.0 - 0x1p-53),
              "round_triple_ rounds down past the midpoint below.");
static_assert(bits_of(round_triple_(1.0, -0x1p-54, 0x1p-120)) == bits_of(1.0),
              "round_triple_ rounds up above the midpoint below.");

}  // namespace fixy::fp::detail::double_word_self_test
