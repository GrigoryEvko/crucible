// Tests of foundation/core/Compare.h and foundation/core/Convert.h at run
// time.  compare, fits and the conversions are compared with a model in a
// 64-bit integer: over each value of the 8-bit and 16-bit types to each of
// those types, over the edges of the 32-bit and 64-bit types, and over
// Philox values.  The conversions of a float are compared with values whose
// truncation the test knows: an integer and a fraction of two bits.

#include <foundation/core/Compare.h>
#include <foundation/core/Convert.h>

#include <foundation/Platform.h>

#include "philox_stream.h"

#include <compare>
#include <cstdint>

namespace {

namespace core = ::foundation::core;
using core::Option;

// The value, through a call that the optimizer cannot read, so each
// operation runs at run time.
template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

template <class T>
[[nodiscard]] bool holds(Option<T> result, T expected) noexcept {
    return result.is_some() && static_cast<Option<T>&&>(result).value_or(T{0}) == expected;
}

// The model of a value of each integer type of at most 32 bits, and of each
// 64-bit value that a signed 64-bit integer holds.
template <class T>
[[nodiscard]] constexpr std::int64_t wide(T value) noexcept {
    return static_cast<std::int64_t>(value);
}

template <class To>
[[nodiscard]] constexpr bool model_fits(std::int64_t value) noexcept {
    return value >= wide(core::min_of<To>) && value <= wide(core::max_of<To>);
}

template <class To>
[[nodiscard]] constexpr To model_clamp(std::int64_t value) noexcept {
    if (value < wide(core::min_of<To>)) return core::min_of<To>;
    if (value > wide(core::max_of<To>)) return core::max_of<To>;
    return static_cast<To>(value);
}

// fits, narrow, wrapping_narrow and saturating_narrow of one value to one
// type of at most 32 bits.
template <class To, class From>
void check_conversion(From input) {
    From const value = opaque(input);
    std::int64_t const exact = wide(value);
    bool const is_held = model_fits<To>(exact);
    CRUCIBLE_FATAL_INVARIANT(core::fits<To>(value) == is_held);
    CRUCIBLE_FATAL_INVARIANT(core::narrow<To>(value).is_some() == is_held);
    CRUCIBLE_FATAL_INVARIANT(!is_held || holds(core::narrow<To>(value), static_cast<To>(exact)));
    CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<To>(value) == model_clamp<To>(exact));
    // The low bits: the value modulo 2^bits, as a To.
    std::uint64_t const mask = core::bits_of<To> == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << core::bits_of<To>)-1;
    std::uint64_t const low = static_cast<std::uint64_t>(exact) & mask;
    CRUCIBLE_FATAL_INVARIANT((static_cast<std::uint64_t>(core::wrapping_narrow<To>(value)) & mask) == low);
}

template <class From>
void check_conversions_of(From value) {
    check_conversion<std::int8_t>(value);
    check_conversion<std::uint8_t>(value);
    check_conversion<std::int16_t>(value);
    check_conversion<std::uint16_t>(value);
    check_conversion<std::int32_t>(value);
    check_conversion<std::uint32_t>(value);
}

void test_each_small_value_to_each_small_type() {
    for (int raw = 0; raw < 65536; ++raw) {
        check_conversions_of(static_cast<std::uint16_t>(raw));
        check_conversions_of(static_cast<std::int16_t>(raw));
        if (raw < 256) {
            check_conversions_of(static_cast<std::uint8_t>(raw));
            check_conversions_of(static_cast<std::int8_t>(raw));
        }
    }
}

// compare of each pair of two 8-bit types, against the order of the model.
template <class Left, class Right>
void check_compare_pairs() {
    for (int first = 0; first < 256; ++first) {
        for (int second = 0; second < 256; ++second) {
            Left const left = opaque(static_cast<Left>(first));
            Right const right = opaque(static_cast<Right>(second));
            std::strong_ordering const expected = wide(left) <=> wide(right);
            CRUCIBLE_FATAL_INVARIANT(core::compare(left, right) == expected);
            CRUCIBLE_FATAL_INVARIANT(core::compare(right, left) == (wide(right) <=> wide(left)));
        }
    }
}

void test_compare_of_each_pair_of_bytes() {
    check_compare_pairs<std::int8_t, std::int8_t>();
    check_compare_pairs<std::int8_t, std::uint8_t>();
    check_compare_pairs<std::uint8_t, std::uint8_t>();
    check_compare_pairs<std::int8_t, std::uint64_t>();
    check_compare_pairs<std::uint8_t, std::int64_t>();
}

// The edges of the 64-bit types, where a model in 64 bits cannot hold each
// value: the compare and the conversions are stated by hand.
void test_edges_of_the_wide_types() {
    std::uint64_t const highest = core::max_of<std::uint64_t>;
    std::int64_t const lowest = core::min_of<std::int64_t>;
    CRUCIBLE_FATAL_INVARIANT(std::is_lt(core::compare(opaque(lowest), opaque(highest))));
    CRUCIBLE_FATAL_INVARIANT(std::is_gt(core::compare(opaque(highest), opaque(lowest))));
    CRUCIBLE_FATAL_INVARIANT(std::is_lt(core::compare(opaque(std::int64_t{-1}), opaque(highest))));
    CRUCIBLE_FATAL_INVARIANT(std::is_gt(core::compare(opaque(highest), opaque(core::max_of<std::int64_t>))));
    CRUCIBLE_FATAL_INVARIANT(
        std::is_gt(core::compare(opaque(std::uint64_t{1} << 63), opaque(core::max_of<std::int64_t>))));
    CRUCIBLE_FATAL_INVARIANT(std::is_eq(core::compare(opaque(std::uint64_t{5}), opaque(std::int32_t{5}))));
    CRUCIBLE_FATAL_INVARIANT(!core::fits<std::int64_t>(opaque(highest)));
    CRUCIBLE_FATAL_INVARIANT(core::fits<std::int64_t>(opaque(highest >> 1)));
    CRUCIBLE_FATAL_INVARIANT(!core::fits<std::uint64_t>(opaque(lowest)));
    CRUCIBLE_FATAL_INVARIANT(core::narrow<std::int64_t>(opaque(highest)).is_none());
    CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<std::int64_t>(opaque(highest)) == core::max_of<std::int64_t>);
    CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<std::uint64_t>(opaque(lowest)) == 0);
    CRUCIBLE_FATAL_INVARIANT(core::wrapping_narrow<std::int64_t>(opaque(highest)) == -1);
    CRUCIBLE_FATAL_INVARIANT(
        holds(core::narrow<std::uint32_t>(opaque(std::int64_t{4294967295})), std::uint32_t{4294967295u}));
    CRUCIBLE_FATAL_INVARIANT(core::narrow<std::uint32_t>(opaque(std::int64_t{4294967296})).is_none());
    CRUCIBLE_FATAL_INVARIANT(core::widen<std::int64_t>(opaque(std::uint32_t{4294967295u})) == 4294967295);
    CRUCIBLE_FATAL_INVARIANT(core::widen<double>(opaque(std::int32_t{-7})) <= -7.0);
}

// The round trip of Philox values: narrow gives a value exactly when the
// value comes back, and saturating_narrow is the clamp.
template <class To>
void check_round_trip(std::int64_t value) {
    std::int64_t const kept = opaque(value);
    bool const is_held = core::narrow<To>(kept).is_some();
    CRUCIBLE_FATAL_INVARIANT(is_held == model_fits<To>(kept));
    if (is_held) {
        To const narrowed = core::narrow<To>(kept).expect("the model holds the value");
        CRUCIBLE_FATAL_INVARIANT(core::widen<std::int64_t>(narrowed) == kept);
    }
    CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<To>(kept) == model_clamp<To>(kept));
}

void test_round_trip_of_philox_values() {
    ::crucible::test::PhiloxStream stream{42};
    for (int index = 0; index < 200000; ++index) {
        // A random width reaches the edges of each type.
        std::int64_t const value = static_cast<std::int64_t>(stream.next_wide()) >> stream.below(64);
        check_round_trip<std::int8_t>(value);
        check_round_trip<std::uint8_t>(value);
        check_round_trip<std::int16_t>(value);
        check_round_trip<std::uint16_t>(value);
        check_round_trip<std::int32_t>(value);
        check_round_trip<std::uint32_t>(value);
    }
}

// narrow of a float truncates toward zero.  A value k + q / 4 of an integer
// k and q in 0 to 3 is exact in a double, and its truncation is k, or k + 1
// for a negative k with q > 0.
void test_narrow_of_a_double_truncates_toward_zero() {
    ::crucible::test::PhiloxStream stream{43};
    for (int index = 0; index < 200000; ++index) {
        auto const whole = static_cast<std::int32_t>(stream.next_word());
        auto const quarters = static_cast<int>(stream.below(4));
        double const value = static_cast<double>(whole) + 0.25 * quarters;
        std::int64_t const truncated = whole < 0 && quarters > 0 ? std::int64_t{whole} + 1 : std::int64_t{whole};
        CRUCIBLE_FATAL_INVARIANT(holds(core::narrow<std::int64_t>(opaque(value)), truncated));
        CRUCIBLE_FATAL_INVARIANT(core::narrow<std::int32_t>(opaque(value)).is_some()
                                 == model_fits<std::int32_t>(truncated));
        CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<std::int16_t>(opaque(value))
                                 == model_clamp<std::int16_t>(truncated));
        CRUCIBLE_FATAL_INVARIANT(core::narrow<std::uint32_t>(opaque(value)).is_some()
                                 == model_fits<std::uint32_t>(truncated));
    }
}

// The edges of the conversion of a float: a NaN, the infinities, -0, the
// bounds of the range and the first value past each bound.
void test_edges_of_a_float() {
    double const nan = core::quiet_nan_of<double>;
    double const infinity = core::infinity_of<double>;
    CRUCIBLE_FATAL_INVARIANT(core::narrow<int>(opaque(nan)).is_none() && core::narrow<int>(opaque(infinity)).is_none());
    CRUCIBLE_FATAL_INVARIANT(core::narrow<int>(opaque(-infinity)).is_none());
    CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<int>(opaque(nan)) == 0);
    CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<int>(opaque(infinity)) == core::max_of<int>);
    CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<int>(opaque(-infinity)) == core::min_of<int>);
    CRUCIBLE_FATAL_INVARIANT(holds(core::narrow<int>(opaque(-0.0)), 0)
                             && holds(core::narrow<unsigned>(opaque(-0.5)), 0u));
    CRUCIBLE_FATAL_INVARIANT(core::narrow<unsigned>(opaque(-1.0)).is_none());
    CRUCIBLE_FATAL_INVARIANT(holds(core::narrow<int>(opaque(-2147483648.9)), core::min_of<int>));
    CRUCIBLE_FATAL_INVARIANT(core::narrow<int>(opaque(-2147483649.0)).is_none());
    CRUCIBLE_FATAL_INVARIANT(holds(core::narrow<int>(opaque(2147483647.9)), core::max_of<int>));
    CRUCIBLE_FATAL_INVARIANT(core::narrow<int>(opaque(2147483648.0)).is_none());
    // 2^63 is the first double past the range of a signed 64-bit integer,
    // and -2^63 is in it.
    CRUCIBLE_FATAL_INVARIANT(core::narrow<std::int64_t>(opaque(9223372036854775808.0)).is_none());
    CRUCIBLE_FATAL_INVARIANT(
        holds(core::narrow<std::int64_t>(opaque(-9223372036854775808.0)), core::min_of<std::int64_t>));
    CRUCIBLE_FATAL_INVARIANT(
        holds(core::narrow<std::int64_t>(opaque(9223372036854774784.0)), std::int64_t{9223372036854774784}));
    CRUCIBLE_FATAL_INVARIANT(core::narrow<std::uint64_t>(opaque(18446744073709551616.0)).is_none());
    CRUCIBLE_FATAL_INVARIANT(
        holds(core::narrow<std::uint64_t>(opaque(18446744073709549568.0)), std::uint64_t{18446744073709549568u}));
    // A float has 24 bits of significand: -2^31 is its first value in the
    // range of a signed 32-bit integer.
    CRUCIBLE_FATAL_INVARIANT(holds(core::narrow<int>(opaque(-2147483648.0f)), core::min_of<int>));
    CRUCIBLE_FATAL_INVARIANT(core::narrow<int>(opaque(2147483648.0f)).is_none());
    CRUCIBLE_FATAL_INVARIANT(holds(core::narrow<std::int16_t>(opaque(-32768.9f)), std::int16_t{-32768}));
    CRUCIBLE_FATAL_INVARIANT(core::narrow<std::int16_t>(opaque(-32769.0f)).is_none());
    CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<std::uint8_t>(opaque(300.5f)) == 255);
    CRUCIBLE_FATAL_INVARIANT(core::saturating_narrow<std::uint8_t>(opaque(-0.5f)) == 0);
}

// min, max and clamp of each pair of bytes, and the rule of the floats.
void test_min_max_and_clamp() {
    for (int first = -128; first < 128; ++first) {
        for (int second = -128; second < 128; ++second) {
            auto const left = opaque(static_cast<std::int8_t>(first));
            auto const right = opaque(static_cast<std::int8_t>(second));
            CRUCIBLE_FATAL_INVARIANT(core::min(left, right) == (first < second ? left : right));
            CRUCIBLE_FATAL_INVARIANT(core::max(left, right) == (first < second ? right : left));
            CRUCIBLE_FATAL_INVARIANT(core::abs_diff(left, right)
                                     == static_cast<std::uint8_t>(first < second ? second - first : first - second));
            core::Bounds<std::int8_t>::of(left, right)
                .match(
                    [&](core::Bounds<std::int8_t> range) {
                        for (int probe = -128; probe < 128; probe += 7) {
                            auto const value = static_cast<std::int8_t>(probe);
                            std::int8_t const expected = probe < first ? left : (probe > second ? right : value);
                            CRUCIBLE_FATAL_INVARIANT(core::clamp(opaque(value), range) == expected);
                        }
                    },
                    [&] { CRUCIBLE_FATAL_INVARIANT(first > second); });
        }
        auto const value = opaque(static_cast<std::int8_t>(first));
        CRUCIBLE_FATAL_INVARIANT(core::unsigned_abs(value) == static_cast<std::uint8_t>(first < 0 ? -first : first));
    }
    double const nan = core::quiet_nan_of<double>;
    CRUCIBLE_FATAL_INVARIANT(core::min(opaque(1.0), opaque(nan)) <= 1.0);
    CRUCIBLE_FATAL_INVARIANT(core::max(opaque(1.0), opaque(nan)) >= 1.0);
    CRUCIBLE_FATAL_INVARIANT(core::clamp(opaque(nan), core::bounds<-1.0, 1.0>()) <= -1.0);
    CRUCIBLE_FATAL_INVARIANT(core::Bounds<double>::of(opaque(nan), 1.0).is_none());
}

}  // namespace

int main() {
    test_each_small_value_to_each_small_type();
    test_compare_of_each_pair_of_bytes();
    test_edges_of_the_wide_types();
    test_round_trip_of_philox_values();
    test_narrow_of_a_double_truncates_toward_zero();
    test_edges_of_a_float();
    test_min_max_and_clamp();
    return 0;
}
