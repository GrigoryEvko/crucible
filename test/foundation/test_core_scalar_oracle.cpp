// The differential test of the Scalar family against the std forms.
//
// For each input that a std form defines, the operation of the family gives
// the answer of the std form.  The test covers the limits of <limits>, the
// bit operations of <bit>, the exact compares of <utility>, the saturating
// arithmetic and gcd of <numeric>, bit_cast, the float functions of <cmath>
// and the total order of <compare>.  For an input that the std form does not
// define, such as std::bit_ceil of a value above the top bit, the test checks
// only the family.  The std sqrt of a negative value or of a NaN gives a NaN
// whose bits change with the target, and fp::sqrt gives the canonical quiet
// NaN, so the test compares only the class of such a result.  Each std form
// stays in the one opt-out region of the class ORACLE, and each function of
// the region takes and gives plain values only.

#include <foundation/core/Arithmetic.h>
#include <foundation/core/Array.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Compare.h>
#include <foundation/core/Convert.h>
#include <foundation/core/Float.h>
#include <foundation/core/Number.h>
#include <foundation/core/Scalar.h>

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>

#include "philox_stream.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>

namespace {

namespace core = ::foundation::core;
namespace fp = ::foundation::core::fp;

// The order of two values as a number: -1, 0 or 1.
[[nodiscard]] int order_number(std::strong_ordering order) noexcept {
    return std::is_lt(order) ? -1 : (std::is_gt(order) ? 1 : 0);
}

template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the differential test of the Scalar family against the std forms")

template <class T>
consteval bool integer_limits_agree() noexcept {
    return core::max_of<T> == std::numeric_limits<T>::max() && core::min_of<T> == std::numeric_limits<T>::min()
        && core::value_bits_of<T> == std::numeric_limits<T>::digits;
}

static_assert(integer_limits_agree<std::int8_t>() && integer_limits_agree<std::int16_t>()
              && integer_limits_agree<std::int32_t>() && integer_limits_agree<std::int64_t>());
static_assert(integer_limits_agree<std::uint8_t>() && integer_limits_agree<std::uint16_t>()
              && integer_limits_agree<std::uint32_t>() && integer_limits_agree<std::uint64_t>());

// min_of of a Float is the lowest value, and min_normal_of is the min() of
// std::numeric_limits.  The test compares the bits, so a NaN compares too.
template <class T, class Word>
consteval bool float_limits_agree() noexcept {
    using Limits = std::numeric_limits<T>;
    return std::bit_cast<Word>(core::max_of<T>) == std::bit_cast<Word>(Limits::max())
        && std::bit_cast<Word>(core::min_of<T>) == std::bit_cast<Word>(Limits::lowest())
        && std::bit_cast<Word>(core::min_normal_of<T>) == std::bit_cast<Word>(Limits::min())
        && std::bit_cast<Word>(core::min_subnormal_of<T>) == std::bit_cast<Word>(Limits::denorm_min())
        && std::bit_cast<Word>(core::epsilon_of<T>) == std::bit_cast<Word>(Limits::epsilon())
        && std::bit_cast<Word>(core::infinity_of<T>) == std::bit_cast<Word>(Limits::infinity())
        && std::bit_cast<Word>(core::quiet_nan_of<T>) == std::bit_cast<Word>(Limits::quiet_NaN())
        && std::bit_cast<Word>(core::signaling_nan_of<T>) == std::bit_cast<Word>(Limits::signaling_NaN())
        && core::value_bits_of<T> == Limits::digits;
}

static_assert(float_limits_agree<float, std::uint32_t>() && float_limits_agree<double, std::uint64_t>());

[[nodiscard]] int std_popcount(std::uint64_t value) noexcept { return std::popcount(value); }
[[nodiscard]] int std_countl_zero(std::uint64_t value) noexcept { return std::countl_zero(value); }
[[nodiscard]] int std_countl_zero_short(std::uint16_t value) noexcept { return std::countl_zero(value); }
[[nodiscard]] int std_countr_zero(std::uint64_t value) noexcept { return std::countr_zero(value); }
[[nodiscard]] int std_countl_one(std::uint64_t value) noexcept { return std::countl_one(value); }
[[nodiscard]] int std_countr_one(std::uint64_t value) noexcept { return std::countr_one(value); }
[[nodiscard]] int std_bit_width(std::uint64_t value) noexcept { return std::bit_width(value); }
[[nodiscard]] bool std_has_single_bit(std::uint64_t value) noexcept { return std::has_single_bit(value); }
[[nodiscard]] std::uint64_t std_bit_floor(std::uint64_t value) noexcept { return std::bit_floor(value); }

// std::bit_ceil is defined only for a value of at most 2^63.
[[nodiscard]] std::uint64_t std_bit_ceil(std::uint64_t value) noexcept { return std::bit_ceil(value); }

[[nodiscard]] std::uint64_t std_rotl(std::uint64_t value, int count) noexcept { return std::rotl(value, count); }
[[nodiscard]] std::uint64_t std_rotr(std::uint64_t value, int count) noexcept { return std::rotr(value, count); }
[[nodiscard]] std::uint64_t std_byteswap(std::uint64_t value) noexcept { return std::byteswap(value); }
[[nodiscard]] std::int16_t std_byteswap_short(std::int16_t value) noexcept { return std::byteswap(value); }

[[nodiscard]] int std_order(std::int64_t left, std::uint64_t right) noexcept {
    if (std::cmp_less(left, right)) return -1;
    return std::cmp_greater(left, right) ? 1 : 0;
}

[[nodiscard]] int std_order_short(std::uint32_t left, std::int16_t right) noexcept {
    if (std::cmp_less(left, right)) return -1;
    return std::cmp_greater(left, right) ? 1 : 0;
}

[[nodiscard]] bool std_in_range_int8(std::int64_t value) noexcept { return std::in_range<std::int8_t>(value); }
[[nodiscard]] bool std_in_range_uint32(std::int64_t value) noexcept { return std::in_range<std::uint32_t>(value); }
[[nodiscard]] bool std_in_range_int32(std::uint64_t value) noexcept { return std::in_range<std::int32_t>(value); }

[[nodiscard]] std::int8_t std_saturating_cast_int8(std::int64_t value) noexcept {
    return std::saturating_cast<std::int8_t>(value);
}

[[nodiscard]] std::uint32_t std_saturating_cast_uint32(std::int64_t value) noexcept {
    return std::saturating_cast<std::uint32_t>(value);
}

[[nodiscard]] std::int64_t std_min(std::int64_t left, std::int64_t right) noexcept { return std::min(left, right); }
[[nodiscard]] std::int64_t std_max(std::int64_t left, std::int64_t right) noexcept { return std::max(left, right); }

[[nodiscard]] std::int64_t std_clamp(std::int64_t value, std::int64_t lo, std::int64_t hi) noexcept {
    return std::clamp(value, lo, hi);
}

[[nodiscard]] std::int32_t std_saturating_add(std::int32_t left, std::int32_t right) noexcept {
    return std::saturating_add(left, right);
}

[[nodiscard]] std::int32_t std_saturating_sub(std::int32_t left, std::int32_t right) noexcept {
    return std::saturating_sub(left, right);
}

[[nodiscard]] std::int32_t std_saturating_mul(std::int32_t left, std::int32_t right) noexcept {
    return std::saturating_mul(left, right);
}

[[nodiscard]] std::uint32_t std_saturating_add_unsigned(std::uint32_t left, std::uint32_t right) noexcept {
    return std::saturating_add(left, right);
}

[[nodiscard]] std::uint32_t std_saturating_sub_unsigned(std::uint32_t left, std::uint32_t right) noexcept {
    return std::saturating_sub(left, right);
}

[[nodiscard]] std::uint32_t std_saturating_mul_unsigned(std::uint32_t left, std::uint32_t right) noexcept {
    return std::saturating_mul(left, right);
}

// std::gcd is defined only when the magnitude of each value fits its type.
[[nodiscard]] std::int64_t std_gcd(std::int64_t left, std::int64_t right) noexcept { return std::gcd(left, right); }

[[nodiscard]] std::uint64_t std_gcd_unsigned(std::uint64_t left, std::uint64_t right) noexcept {
    return std::gcd(left, right);
}

[[nodiscard]] std::uint64_t std_bits(double value) noexcept { return std::bit_cast<std::uint64_t>(value); }
[[nodiscard]] std::uint32_t std_bits_float(float value) noexcept { return std::bit_cast<std::uint32_t>(value); }

// The classes of a value as one number, one bit for each std test.
[[nodiscard]] int std_classes(double value) noexcept {
    return (std::isnan(value) ? 1 : 0) | (std::isinf(value) ? 2 : 0) | (std::isfinite(value) ? 4 : 0)
         | (std::isnormal(value) ? 8 : 0) | (std::signbit(value) ? 16 : 0);
}

[[nodiscard]] int std_classes_float(float value) noexcept {
    return (std::isnan(value) ? 1 : 0) | (std::isinf(value) ? 2 : 0) | (std::isfinite(value) ? 4 : 0)
         | (std::isnormal(value) ? 8 : 0) | (std::signbit(value) ? 16 : 0);
}

[[nodiscard]] double std_fabs(double value) noexcept { return std::fabs(value); }
[[nodiscard]] float std_fabs_float(float value) noexcept { return std::fabs(value); }
[[nodiscard]] double std_copysign(double value, double sign) noexcept { return std::copysign(value, sign); }
[[nodiscard]] double std_sqrt(double value) noexcept { return std::sqrt(value); }
[[nodiscard]] float std_sqrt_float(float value) noexcept { return std::sqrt(value); }

[[nodiscard]] double std_fma(double left, double right, double addend) noexcept {
    return std::fma(left, right, addend);
}

[[nodiscard]] float std_fma_float(float left, float right, float addend) noexcept {
    return std::fma(left, right, addend);
}

[[nodiscard]] int std_total_order(double left, double right) noexcept {
    return order_number(std::strong_order(left, right));
}

[[nodiscard]] int std_total_order_float(float left, float right) noexcept {
    return order_number(std::strong_order(left, right));
}

[[nodiscard]] double std_min_double(double left, double right) noexcept { return std::min(left, right); }
[[nodiscard]] double std_max_double(double left, double right) noexcept { return std::max(left, right); }

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// A word of each magnitude: the shift makes a small word as frequent as a
// large word.
[[nodiscard]] std::uint64_t draw_word(::foundation::test::PhiloxStream& stream) noexcept {
    return stream.next_wide() >> stream.below(64);
}

// A signed value of each magnitude and of each sign.
[[nodiscard]] std::int64_t draw_signed(::foundation::test::PhiloxStream& stream) noexcept {
    std::uint64_t const magnitude = draw_word(stream);
    return static_cast<std::int64_t>(stream.below(2) == 0 ? magnitude : 0 - magnitude);
}

void check_bits(std::uint64_t word, int count) {
    CRUCIBLE_FATAL_INVARIANT(core::popcount(word) == std_popcount(word));
    CRUCIBLE_FATAL_INVARIANT(core::countl_zero(word) == std_countl_zero(word));
    CRUCIBLE_FATAL_INVARIANT(core::countr_zero(word) == std_countr_zero(word));
    CRUCIBLE_FATAL_INVARIANT(core::countl_one(word) == std_countl_one(word));
    CRUCIBLE_FATAL_INVARIANT(core::countr_one(word) == std_countr_one(word));
    CRUCIBLE_FATAL_INVARIANT(core::bit_width(word) == std_bit_width(word));
    CRUCIBLE_FATAL_INVARIANT(core::has_single_bit(word) == std_has_single_bit(word));
    CRUCIBLE_FATAL_INVARIANT(core::bit_floor(word) == std_bit_floor(word));
    auto const short_word = static_cast<std::uint16_t>(word);
    CRUCIBLE_FATAL_INVARIANT(core::countl_zero(short_word) == std_countl_zero_short(short_word));
    // A ceiling is never 0, so the fallback 0 shows an empty Option.
    if (word <= (std::uint64_t{1} << 63)) {
        CRUCIBLE_FATAL_INVARIANT(core::bit_ceil(word).value_or(0) == std_bit_ceil(word));
    } else {
        CRUCIBLE_FATAL_INVARIANT(core::bit_ceil(word).is_none());
    }
    CRUCIBLE_FATAL_INVARIANT(core::rotl(word, count) == std_rotl(word, count));
    CRUCIBLE_FATAL_INVARIANT(core::rotr(word, count) == std_rotr(word, count));
    CRUCIBLE_FATAL_INVARIANT(core::byteswap(word) == std_byteswap(word));
    auto const signed_short = static_cast<std::int16_t>(word);
    CRUCIBLE_FATAL_INVARIANT(core::byteswap(signed_short) == std_byteswap_short(signed_short));
}

// The bit operations agree with <bit> for each word and each count, also
// for the minimum of int as a count.  Linear in the count of trials.
void test_bit_operations_agree() {
    ::foundation::test::PhiloxStream stream{0x5EED00000000B175u};
    for (int trial = 0; trial < 20000; ++trial) {
        std::uint64_t const word = opaque(draw_word(stream));
        check_bits(word, opaque(static_cast<int>(stream.next_word())));
        check_bits(word, opaque(static_cast<int>(stream.below(131)) - 65));
    }
    core::FixedArray<std::uint64_t, 6> const words{std::uint64_t{0},
                                                   std::uint64_t{1},
                                                   std::uint64_t{2},
                                                   std::uint64_t{1} << 63,
                                                   (std::uint64_t{1} << 63) + 1,
                                                   core::max_of<std::uint64_t>};
    core::FixedArray<int, 6> const counts{core::min_of<int>, core::max_of<int>, 0, -1, 64, -64};
    for (std::uint64_t const word : words) {
        for (int const count : counts) {
            check_bits(opaque(word), opaque(count));
        }
    }
}

void check_compares(std::int64_t signed_value, std::uint64_t unsigned_value, std::int64_t other) {
    CRUCIBLE_FATAL_INVARIANT(order_number(core::compare(signed_value, unsigned_value))
                             == std_order(signed_value, unsigned_value));
    auto const left = static_cast<std::uint32_t>(unsigned_value);
    auto const right = static_cast<std::int16_t>(signed_value);
    CRUCIBLE_FATAL_INVARIANT(order_number(core::compare(left, right)) == std_order_short(left, right));
    bool const fits_byte = std_in_range_int8(signed_value);
    CRUCIBLE_FATAL_INVARIANT(core::fits<std::int8_t>(signed_value) == fits_byte);
    CRUCIBLE_FATAL_INVARIANT(core::narrow<std::int8_t>(signed_value).is_some() == fits_byte);
    CRUCIBLE_FATAL_INVARIANT(!fits_byte || core::narrow<std::int8_t>(signed_value).value_or(0) == signed_value);
    CRUCIBLE_FATAL_INVARIANT(core::fits<std::uint32_t>(signed_value) == std_in_range_uint32(signed_value));
    CRUCIBLE_FATAL_INVARIANT(core::fits<std::int32_t>(unsigned_value) == std_in_range_int32(unsigned_value));
    CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<std::int8_t>(signed_value)
                             == std_saturating_cast_int8(signed_value));
    CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<std::uint32_t>(signed_value)
                             == std_saturating_cast_uint32(signed_value));
    CRUCIBLE_FATAL_INVARIANT(core::min(signed_value, other) == std_min(signed_value, other));
    CRUCIBLE_FATAL_INVARIANT(core::max(signed_value, other) == std_max(signed_value, other));
    std::int64_t const lo = std_min(other, signed_value);
    std::int64_t const hi = std_max(other, signed_value);
    core::Bounds<std::int64_t> const range =
        core::Bounds<std::int64_t>::of(lo, hi).expect("the minimum of two values is not more than their maximum");
    std::int64_t const value = static_cast<std::int64_t>(unsigned_value);
    CRUCIBLE_FATAL_INVARIANT(core::clamp(value, range) == std_clamp(value, lo, hi));
}

// The exact compares, the range tests and the narrowing agree with
// std::cmp_less, std::in_range and std::saturating_cast, and min, max and
// clamp agree with <algorithm> for integers.  Linear in the count of
// trials.
void test_compares_agree() {
    ::foundation::test::PhiloxStream stream{0x5EED00000000C0A7u};
    for (int trial = 0; trial < 20000; ++trial) {
        check_compares(opaque(draw_signed(stream)), opaque(draw_word(stream)), opaque(draw_signed(stream)));
    }
    core::FixedArray<std::int64_t, 8> const edges{
        core::min_of<std::int64_t>, core::max_of<std::int64_t>, std::int64_t{-1},         std::int64_t{0},
        std::int64_t{127},          std::int64_t{-129},         std::int64_t{4294967295}, std::int64_t{4294967296}};
    for (std::int64_t const edge : edges) {
        for (std::int64_t const other : edges) {
            check_compares(opaque(edge), opaque(static_cast<std::uint64_t>(other)), opaque(other));
        }
    }
}

void check_arithmetic(std::int32_t left, std::int32_t right) {
    CRUCIBLE_FATAL_INVARIANT(core::saturating_add(left, right) == std_saturating_add(left, right));
    CRUCIBLE_FATAL_INVARIANT(core::saturating_sub(left, right) == std_saturating_sub(left, right));
    CRUCIBLE_FATAL_INVARIANT(core::saturating_mul(left, right) == std_saturating_mul(left, right));
    auto const first = static_cast<std::uint32_t>(left);
    auto const second = static_cast<std::uint32_t>(right);
    CRUCIBLE_FATAL_INVARIANT(core::saturating_add(first, second) == std_saturating_add_unsigned(first, second));
    CRUCIBLE_FATAL_INVARIANT(core::saturating_sub(first, second) == std_saturating_sub_unsigned(first, second));
    CRUCIBLE_FATAL_INVARIANT(core::saturating_mul(first, second) == std_saturating_mul_unsigned(first, second));
}

void check_gcd(std::int64_t left, std::int64_t right) {
    CRUCIBLE_FATAL_INVARIANT(core::gcd(left, right) == static_cast<std::uint64_t>(std_gcd(left, right)));
    auto const first = static_cast<std::uint64_t>(left);
    auto const second = static_cast<std::uint64_t>(right);
    CRUCIBLE_FATAL_INVARIANT(core::gcd(first, second) == std_gcd_unsigned(first, second));
}

// The saturating arithmetic and gcd agree with <numeric>.  A shared factor
// gives a gcd other than 1.  gcd of the minimum of a signed type has no std
// answer, so the factors are below 2^21.  Linear in the count of trials.
void test_arithmetic_agrees() {
    ::foundation::test::PhiloxStream stream{0x5EED00000000A417u};
    for (int trial = 0; trial < 20000; ++trial) {
        check_arithmetic(opaque(static_cast<std::int32_t>(draw_signed(stream))),
                         opaque(static_cast<std::int32_t>(draw_signed(stream))));
        auto const factor = static_cast<std::int64_t>(stream.below(std::uint64_t{1} << 20) + 1);
        auto const left = factor * static_cast<std::int64_t>(stream.below(std::uint64_t{1} << 21));
        auto const right = factor * static_cast<std::int64_t>(stream.below(std::uint64_t{1} << 21));
        check_gcd(opaque(stream.below(2) == 0 ? left : -left), opaque(right));
    }
    core::FixedArray<std::int32_t, 6> const edges{
        core::min_of<std::int32_t>, core::max_of<std::int32_t>, 0, -1, 1, 46341};
    for (std::int32_t const edge : edges) {
        for (std::int32_t const other : edges) {
            check_arithmetic(opaque(edge), opaque(other));
            check_gcd(opaque(std::int64_t{edge}), opaque(std::int64_t{other}));
        }
    }
}

template <class T>
[[nodiscard]] int family_classes(T value) noexcept {
    return (fp::is_nan(value) ? 1 : 0) | (fp::is_inf(value) ? 2 : 0) | (fp::is_finite(value) ? 4 : 0)
         | (fp::is_normal(value) ? 8 : 0) | (fp::sign_bit(value) ? 16 : 0);
}

[[nodiscard]] std::uint64_t bits_of_double(double value) noexcept { return core::bit_cast<std::uint64_t>(value); }
[[nodiscard]] std::uint32_t bits_of_float(float value) noexcept { return core::bit_cast<std::uint32_t>(value); }

void check_double(double value) {
    CRUCIBLE_FATAL_INVARIANT(bits_of_double(value) == std_bits(value));
    CRUCIBLE_FATAL_INVARIANT(family_classes(value) == std_classes(value));
    CRUCIBLE_FATAL_INVARIANT(bits_of_double(fp::abs(value)) == std_bits(std_fabs(value)));
    double const root = fp::sqrt(value);
    double const expected = std_sqrt(value);
    if ((std_classes(expected) & 1) != 0) {
        CRUCIBLE_FATAL_INVARIANT(bits_of_double(root) == bits_of_double(core::quiet_nan_of<double>));
    } else {
        CRUCIBLE_FATAL_INVARIANT(bits_of_double(root) == std_bits(expected));
    }
}

void check_double_pair(double left, double right) {
    CRUCIBLE_FATAL_INVARIANT(bits_of_double(fp::copy_sign(left, right)) == std_bits(std_copysign(left, right)));
    CRUCIBLE_FATAL_INVARIANT(bits_of_double(core::min(left, right)) == std_bits(std_min_double(left, right)));
    CRUCIBLE_FATAL_INVARIANT(bits_of_double(core::max(left, right)) == std_bits(std_max_double(left, right)));
    CRUCIBLE_FATAL_INVARIANT(order_number(core::compare(fp::total_key(left), fp::total_key(right)))
                             == std_total_order(left, right));
}

void check_double_triple(double left, double right, double addend) {
    double const fused = fp::fma(left, right, addend);
    double const expected = std_fma(left, right, addend);
    if ((std_classes(expected) & 1) != 0) {
        CRUCIBLE_FATAL_INVARIANT(fp::is_nan(fused));
    } else {
        CRUCIBLE_FATAL_INVARIANT(bits_of_double(fused) == std_bits(expected));
    }
}

void check_float(float value, float other, float addend) {
    CRUCIBLE_FATAL_INVARIANT(bits_of_float(value) == std_bits_float(value));
    CRUCIBLE_FATAL_INVARIANT(family_classes(value) == std_classes_float(value));
    CRUCIBLE_FATAL_INVARIANT(bits_of_float(fp::abs(value)) == std_bits_float(std_fabs_float(value)));
    float const root = fp::sqrt(value);
    float const expected_root = std_sqrt_float(value);
    if ((std_classes_float(expected_root) & 1) != 0) {
        CRUCIBLE_FATAL_INVARIANT(bits_of_float(root) == bits_of_float(core::quiet_nan_of<float>));
    } else {
        CRUCIBLE_FATAL_INVARIANT(bits_of_float(root) == std_bits_float(expected_root));
    }
    CRUCIBLE_FATAL_INVARIANT(order_number(core::compare(fp::total_key(value), fp::total_key(other)))
                             == std_total_order_float(value, other));
    float const fused = fp::fma(value, other, addend);
    float const expected_fused = std_fma_float(value, other, addend);
    if ((std_classes_float(expected_fused) & 1) != 0) {
        CRUCIBLE_FATAL_INVARIANT(fp::is_nan(fused));
    } else {
        CRUCIBLE_FATAL_INVARIANT(bits_of_float(fused) == std_bits_float(expected_fused));
    }
}

// The bits of each special double: each zero, each one, each infinity, a
// quiet NaN of each sign, two signaling NaNs, the smallest normal value,
// the smallest and the largest subnormal value, the highest and the lowest
// value, 0.5 and 2.
constexpr core::FixedArray<std::uint64_t, 17> kSpecialDoubles{
    0x0000000000000000u, 0x8000000000000000u, 0x3FF0000000000000u, 0xBFF0000000000000u, 0x7FF0000000000000u,
    0xFFF0000000000000u, 0x7FF8000000000000u, 0xFFF8000000000000u, 0x7FF4000000000000u, 0x7FF0000000000001u,
    0x0010000000000000u, 0x0000000000000001u, 0x800FFFFFFFFFFFFFu, 0x7FEFFFFFFFFFFFFFu, 0xFFEFFFFFFFFFFFFFu,
    0x3FE0000000000000u, 0x4000000000000000u};

// The same specials of float.
constexpr core::FixedArray<std::uint32_t, 17> kSpecialFloats{
    0x00000000u, 0x80000000u, 0x3F800000u, 0xBF800000u, 0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC00000u, 0x7FA00000u,
    0x7F800001u, 0x00800000u, 0x00000001u, 0x807FFFFFu, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x3F000000u, 0x40000000u};

// The float operations, bit_cast and the total order agree with <cmath>,
// <bit> and std::strong_order for each special value and for random bits.
// A value in [1, 2) makes the fused multiply-add round.  The specials are
// cubic in their count, and the random part is linear in the count of
// trials.
void test_floats_agree() {
    for (std::uint64_t const left_bits : kSpecialDoubles) {
        double const left = core::bit_cast<double>(opaque(left_bits));
        check_double(left);
        for (std::uint64_t const right_bits : kSpecialDoubles) {
            double const right = core::bit_cast<double>(opaque(right_bits));
            check_double_pair(left, right);
            for (std::uint64_t const addend_bits : kSpecialDoubles) {
                check_double_triple(left, right, core::bit_cast<double>(opaque(addend_bits)));
            }
        }
    }
    for (std::uint32_t const value_bits : kSpecialFloats) {
        for (std::uint32_t const other_bits : kSpecialFloats) {
            for (std::uint32_t const addend_bits : kSpecialFloats) {
                check_float(core::bit_cast<float>(opaque(value_bits)), core::bit_cast<float>(opaque(other_bits)),
                            core::bit_cast<float>(opaque(addend_bits)));
            }
        }
    }
    ::foundation::test::PhiloxStream stream{0x5EED00000000F10Au};
    constexpr std::uint64_t one_bits = 0x3FF0000000000000u;
    for (int trial = 0; trial < 20000; ++trial) {
        double const left = core::bit_cast<double>(opaque(stream.next_wide()));
        double const right = core::bit_cast<double>(opaque(stream.next_wide()));
        check_double(left);
        check_double_pair(left, right);
        check_double_triple(left, right, core::bit_cast<double>(opaque(stream.next_wide())));
        double const near_left = core::bit_cast<double>(opaque(one_bits | (stream.next_wide() >> 12)));
        double const near_right = core::bit_cast<double>(opaque(one_bits | (stream.next_wide() >> 12)));
        check_double_triple(near_left, near_right,
                            -core::bit_cast<double>(opaque(one_bits | (stream.next_wide() >> 12))));
        check_float(core::bit_cast<float>(opaque(stream.next_word())),
                    core::bit_cast<float>(opaque(stream.next_word())),
                    core::bit_cast<float>(opaque(stream.next_word())));
    }
}

}  // namespace

int main() {
    test_bit_operations_agree();
    test_compares_agree();
    test_arithmetic_agrees();
    test_floats_agree();
    return 0;
}
