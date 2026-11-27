// The compile-time checks of fixy/fp/FmaOfFinite.h.  A constant evaluation
// of fma_of_finite runs the exact form, so each value has the bits of
// fp::fma.

#include <fixy/fp/FmaOfFinite.h>

#include <foundation/core/Scalar.h>

#include <cstdint>

namespace fixy::fp::detail::fma_of_finite_self_test {

[[nodiscard]] consteval std::uint64_t bits_of(double value) noexcept {
    return ::foundation::core::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] consteval std::uint32_t bits_of(float value) noexcept {
    return ::foundation::core::bit_cast<std::uint32_t>(value);
}

// One round: 0.1 * 10 - 1 is 2^-54 exactly in one round, and 0 in two.
static_assert(bits_of(fma_of_finite(0.1, 10.0, -1.0)) == bits_of(0x1p-54), "fma_of_finite rounds one time.");
// 0.1f is 13421773 * 2^-27, so 10 times it is 1 + 2^-26 exactly.
static_assert(bits_of(fma_of_finite(0.1f, 10.0f, -1.0f)) == bits_of(0x1p-26f),
              "fma_of_finite of float rounds one time.");
static_assert(bits_of(fma_of_finite(1.5, 2.0, 0.25)) == bits_of(3.25), "an exact sum keeps its bits.");
static_assert(bits_of(fma_of_finite(1.5f, 2.0f, 0.25f)) == bits_of(3.25f), "an exact sum of floats keeps its bits.");
static_assert(bits_of(fma_of_finite(-0.0, 1.0, -0.0)) == bits_of(-0.0), "-0 * 1 + -0 is -0.");

}  // namespace fixy::fp::detail::fma_of_finite_self_test
