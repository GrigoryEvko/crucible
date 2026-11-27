// The compile-time checks of fixy/fp/Log.h.
//
// Each pin here is also a case of test/fixy/test_fp_elementary.cpp, which runs
// the code of each level, so the constant evaluation and the code give the
// same bits.

#include <fixy/fp/Log.h>

#include <foundation/core/Scalar.h>

#include <cstdint>

namespace fixy::fp::detail::log_self_test {

[[nodiscard]] consteval std::uint64_t bits_of(double value) noexcept {
    return ::foundation::core::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] consteval std::uint32_t bits_of(float value) noexcept {
    return ::foundation::core::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] consteval double double_of(std::uint64_t bits) noexcept {
    return ::foundation::core::bit_cast<double>(bits);
}

[[nodiscard]] consteval float float_of(std::uint32_t bits) noexcept {
    return ::foundation::core::bit_cast<float>(bits);
}

// The identity, the neighbors of 1 on the path near 1, 2 on the table path,
// the smallest subnormal through the normalization by integers, and the
// largest finite value.
static_assert(bits_of(::fixy::fp::log(1.0)) == 0x0000000000000000ULL, "log(1) must be exactly +0.");
static_assert(bits_of(::fixy::fp::log(double_of(0x3FF0000000000001ULL))) == 0x3CAFFFFFFFFFFFFFULL,
              "log of the next double above 1 must be the pinned value.");
static_assert(bits_of(::fixy::fp::log(2.0)) == 0x3FE62E42FEFA39EFULL, "log(2) must be the pinned ln2.");
static_assert(bits_of(::fixy::fp::log(double_of(0x0000000000000001ULL))) == 0xC0874385446D71C3ULL,
              "log of the smallest subnormal must be the pinned value.");
static_assert(bits_of(::fixy::fp::log(double_of(0x7FEFFFFFFFFFFFFFULL))) == 0x40862E42FEFA39EFULL,
              "log of the largest finite value must be the pinned value.");

// The float form: the identity, the smallest subnormal, the largest finite
// value, and one input of the list of midpoint inputs.
static_assert(bits_of(::fixy::fp::log(1.0f)) == 0x00000000u, "logf(1) must be exactly +0.");
static_assert(bits_of(::fixy::fp::log(float_of(0x00000001u))) == 0xC2CE8ED0u,
              "logf of the smallest subnormal must be the correctly rounded value.");
static_assert(bits_of(::fixy::fp::log(float_of(0x7F7FFFFFu))) == 0x42B17218u,
              "logf of the largest finite value must be the correctly rounded value.");
static_assert(bits_of(::fixy::fp::log(float_of(0x41178FEBu))) == 0x400FE5E7u,
              "logf of a midpoint input must take the result of the list.");

// The interval of the float tables that holds 1 has invc = 1 and logc = 0,
// and in the bits 1 is at its middle.  So the fast path adds no logc to r on
// each side of 1, and log(x) near 1 has no cancellation.
inline constexpr std::uint32_t kOneOffset = bits_of(1.0f) - kLogFloatOffset;
inline constexpr std::uint32_t kOneIndex = kOneOffset >> (23 - kLogFloatTableBits);
static_assert(bits_of(kLogFloatInvc[kOneIndex]) == bits_of(1.0), "the interval of 1 must have invc = 1.");
static_assert(bits_of(kLogFloatLogc[kOneIndex]) == bits_of(0.0), "the interval of 1 must have logc = +0.");
static_assert((kOneOffset & ((1u << (23 - kLogFloatTableBits)) - 1u)) == 1u << (22 - kLogFloatTableBits),
              "in the bits, 1 must be at the middle of its interval.");

}  // namespace fixy::fp::detail::log_self_test
