// The compile-time checks of fixy/fp/Erf.h.
//
// Each pin here is also a case of test/fixy/test_fp_elementary.cpp, which runs
// the code of each level, so the constant evaluation and the code give the
// same bits.

#include <fixy/fp/Erf.h>

#include <foundation/core/Scalar.h>

#include <cstdint>

namespace fixy::fp::detail::erf_self_test {

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

// The zeros keep their sign, the smallest subnormal and the largest
// subnormal, 2^-28 on the odd polynomial, 1/2 at the start of the first
// interval, 1 in an interval, and the edge to 1.
static_assert(bits_of(::fixy::fp::erf(-0.0)) == 0x8000000000000000ULL, "erf(-0) must be exactly -0.");
static_assert(bits_of(::fixy::fp::erf(double_of(0x0000000000000001ULL))) == 0x0000000000000001ULL,
              "erf of the smallest subnormal must be the smallest subnormal.");
static_assert(bits_of(::fixy::fp::erf(double_of(0x000FFFFFFFFFFFFFULL))) == 0x00120DD750429B6CULL,
              "erf of the largest subnormal must round one time.");
static_assert(bits_of(::fixy::fp::erf(0x1p-28)) == 0x3E320DD750429B6DULL, "erf(2^-28) must be the pinned value.");
static_assert(bits_of(::fixy::fp::erf(0.5)) == 0x3FE0A7EF5C18EDD2ULL, "erf(1/2) must be the pinned value.");
static_assert(bits_of(::fixy::fp::erf(1.0)) == 0x3FEAF767A741088BULL, "erf(1) must be the pinned value.");
static_assert(bits_of(::fixy::fp::erf(double_of(0x4017AFB48DC96626ULL))) == 0x3FEFFFFFFFFFFFFFULL,
              "erf below the edge to 1 must be the largest double below 1.");
static_assert(bits_of(::fixy::fp::erf(double_of(0x4017AFB48DC96627ULL))) == 0x3FF0000000000000ULL,
              "erf at the edge must be 1.");

// The float form: 1/2, 1, a subnormal result, and the edge to 1.
static_assert(bits_of(::fixy::fp::erf(0.5f)) == 0x3F053F7Bu, "erff(1/2) must be the correctly rounded value.");
static_assert(bits_of(::fixy::fp::erf(1.0f)) == 0x3F57BB3Du, "erff(1) must be the correctly rounded value.");
static_assert(bits_of(::fixy::fp::erf(float_of(0x007FFFFFu))) == 0x00906EB9u,
              "erff of the largest subnormal must be the correctly rounded value.");
static_assert(bits_of(::fixy::fp::erf(float_of(0x407AD444u))) == 0x3F7FFFFFu,
              "erff below the edge to 1 must be the largest float below 1.");

// A row of the float table is its terms and no padding, and it fills one
// cache line, so the address of a row is a shift of the index.
static_assert(sizeof(ErfFloatInterval) == sizeof(ErfFloatInterval{}.terms),
              "a row of kErfFloatTable must hold only its terms.");
static_assert(sizeof(ErfFloatInterval) == __GCC_CONSTRUCTIVE_SIZE, "a row of kErfFloatTable must fill one cache line.");
static_assert(static_cast<int>(sizeof(ErfFloatInterval{}.terms) / sizeof(double)) == kErfFloatDegree + 1,
              "a row of kErfFloatTable must hold the terms of degree 0 to kErfFloatDegree.");

// The last interval holds kErfFloatOneFrom, so each |x| below it has a row.
static_assert(static_cast<double>(kErfFloatFirst + kErfFloatIntervals - 1) / 16.0
                  <= static_cast<double>(kErfFloatOneFrom),
              "the last interval of kErfFloatTable must start at or before kErfFloatOneFrom.");
static_assert(static_cast<double>(kErfFloatOneFrom) < static_cast<double>(kErfFloatFirst + kErfFloatIntervals) / 16.0,
              "the last interval of kErfFloatTable must hold kErfFloatOneFrom.");

}  // namespace fixy::fp::detail::erf_self_test
