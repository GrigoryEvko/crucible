// The compile-time checks of fixy/fp/Exp.h.
//
// A constant evaluation runs the arithmetic of GCC and the exact integer form
// of fp::fma.  Each pin here is also a case of test/fixy/test_fp_elementary.cpp,
// which runs the code of each level, so the two engines give the same bits.

#include <fixy/fp/Exp.h>

#include <foundation/core/Scalar.h>

#include <cstdint>

namespace fixy::fp::detail::exp_self_test {

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

// The identity, e, a result past 2^512 and its mirror, and the overflow edge.
static_assert(bits_of(::fixy::fp::exp(0.0)) == 0x3FF0000000000000ULL, "exp(0) must be exactly 1.");
static_assert(bits_of(::fixy::fp::exp(1.0)) == 0x4005BF0A8B145769ULL, "exp(1) must be the pinned e.");
static_assert(bits_of(::fixy::fp::exp(512.0)) == 0x6E19476504BA852EULL, "exp(512) takes the path past the range.");
static_assert(bits_of(::fixy::fp::exp(-512.0)) == 0x11C44109EDB20931ULL, "exp(-512) takes the path past the range.");
static_assert(bits_of(::fixy::fp::exp(double_of(0x40862E42FEFA39EFULL))) == 0x7FEFFFFFFFFFFF2AULL,
              "exp of the largest input with a finite result must be the pinned value.");

// A subnormal result and the smallest subnormal result, which round at their
// own precision one time.
static_assert(bits_of(::fixy::fp::exp(-740.0)) == 0x0000000000000055ULL, "exp(-740) must be the pinned subnormal.");
static_assert(bits_of(::fixy::fp::exp(double_of(0xC0874910D52D3051ULL))) == 0x0000000000000001ULL,
              "exp at the underflow edge must give the smallest subnormal value.");

// The float form: the identity, e, the overflow edge and the subnormal edge.
static_assert(bits_of(::fixy::fp::exp(0.0f)) == 0x3F800000u, "expf(0) must be exactly 1.");
static_assert(bits_of(::fixy::fp::exp(1.0f)) == 0x402DF854u, "expf(1) must be the correctly rounded e.");
static_assert(bits_of(::fixy::fp::exp(float_of(0x42B17217u))) == 0x7F7FFF84u,
              "expf of the largest input with a finite result must be the pinned value.");
static_assert(bits_of(::fixy::fp::exp(float_of(0xC2AEAC50u))) == 0x007FFFE6u,
              "expf below the subnormal edge takes the slow path.");

}  // namespace fixy::fp::detail::exp_self_test
