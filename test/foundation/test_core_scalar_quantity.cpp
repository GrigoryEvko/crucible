// Tests of foundation/core/Quantity.h at run time.  Each conversion between
// the time units is compared with a model in 64-bit arithmetic, over Philox
// values of 32 bits of each sign and over the limits.  A conversion to a
// finer unit and back gives the number again.  The arithmetic of two
// quantities and of a quantity and a plain number agrees with the
// arithmetic of their numbers, and keeps the unit.

#include <foundation/core/Array.h>
#include <foundation/core/Quantity.h>

#include <foundation/Platform.h>

#include "philox_stream.h"

#include <compare>
#include <cstdint>

namespace {

namespace core = ::foundation::core;
namespace unit = ::foundation::core::unit;

template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

// The number of a converted quantity, or the marker when it has none.
template <class UnitTag, class Rep>
[[nodiscard]] std::int64_t number_of(core::Option<core::Quantity<UnitTag, Rep>> converted, std::int64_t marker) {
    return converted.match([](core::Quantity<UnitTag, Rep> quantity) { return std::int64_t{quantity.raw()}; },
                           [marker] { return marker; });
}

// The model: the number of From in To, or the marker when it overflows Rep
// or is no whole number of To.
template <class To, class From, class Rep>
[[nodiscard]] std::int64_t model_convert(Rep value, std::int64_t marker) noexcept {
    auto const number = static_cast<std::int64_t>(value);
    std::int64_t result = 0;
    if (From::ratio >= To::ratio) {
        result = number * static_cast<std::int64_t>(From::ratio / To::ratio);
    } else {
        auto const divisor = static_cast<std::int64_t>(To::ratio / From::ratio);
        if (number % divisor != 0) return marker;
        result = number / divisor;
    }
    if (result < static_cast<std::int64_t>(core::min_of<Rep>)
        || result > static_cast<std::int64_t>(core::max_of<Rep>)) {
        return marker;
    }
    return result;
}

template <class To, class From, class Rep>
void check_convert(Rep value) {
    // A value that no Rep of 32 bits holds marks the empty result.
    constexpr std::int64_t marker = std::int64_t{1} << 40;
    core::Quantity<From, Rep> const quantity{opaque(value)};
    std::int64_t const converted = number_of(core::convert<To>(quantity), marker);
    std::int64_t const expected = model_convert<To, From>(value, marker);
    CRUCIBLE_FATAL_INVARIANT(converted == expected);
}

template <class Rep>
void check_each_time_pair(Rep value) {
    check_convert<unit::Nanosecond, unit::Second>(value);
    check_convert<unit::Nanosecond, unit::Millisecond>(value);
    check_convert<unit::Microsecond, unit::Millisecond>(value);
    check_convert<unit::Second, unit::Nanosecond>(value);
    check_convert<unit::Second, unit::Millisecond>(value);
    check_convert<unit::Millisecond, unit::Microsecond>(value);
    check_convert<unit::Second, unit::Second>(value);
}

void test_convert_agrees_with_the_model() {
    ::foundation::test::PhiloxStream stream{80};
    for (int index = 0; index < 100000; ++index) {
        // A shift of the draw reaches the small numbers and the whole numbers
        // of each unit.
        auto const wide = stream.next_wide() >> stream.below(40);
        check_each_time_pair(static_cast<std::int32_t>(wide));
        check_each_time_pair(static_cast<std::uint32_t>(wide));
        // 2047 * 10^6 is less than the maximum of std::int32_t.
        check_each_time_pair(static_cast<std::int32_t>(wide % 2048) * 1000000);
    }
    core::FixedArray<std::int32_t, 6> const edges{
        core::min_of<std::int32_t>, core::max_of<std::int32_t>, std::int32_t{0}, std::int32_t{-1}, std::int32_t{2},
        std::int32_t{-2000000000}};
    for (std::int32_t const edge : edges) {
        check_each_time_pair(edge);
    }
    check_each_time_pair(core::max_of<std::uint32_t>);
    check_each_time_pair(std::uint32_t{4});
}

// A conversion to a finer unit and back gives the number again.
void test_convert_round_trips() {
    ::foundation::test::PhiloxStream stream{81};
    for (int index = 0; index < 100000; ++index) {
        auto const value = static_cast<std::int64_t>(stream.next_wide() >> 34) - (std::int64_t{1} << 29);
        core::Quantity<unit::Second, std::int64_t> const seconds{opaque(value)};
        auto const nanoseconds = core::convert<unit::Nanosecond>(seconds).expect("a number below 2^30 seconds fits");
        auto const back = core::convert<unit::Second>(nanoseconds).expect("a whole number of seconds converts back");
        CRUCIBLE_FATAL_INVARIANT(back == seconds);
    }
}

// The arithmetic of quantities is the arithmetic of their numbers.
void test_arithmetic_keeps_the_unit() {
    ::foundation::test::PhiloxStream stream{82};
    for (int index = 0; index < 100000; ++index) {
        auto const left = static_cast<std::int32_t>(stream.next_word());
        auto const right = static_cast<std::int32_t>(stream.next_word());
        using Elements = core::Quantity<unit::Element, std::int32_t>;
        Elements const first{opaque(left)};
        Elements const second{opaque(right)};
        constexpr std::int64_t marker = std::int64_t{1} << 40;
        CRUCIBLE_FATAL_INVARIANT(
            number_of(core::checked_add(first, second), marker)
            == core::checked_add(left, right)
                   .match([](std::int32_t sum) { return std::int64_t{sum}; }, [] { return marker; }));
        CRUCIBLE_FATAL_INVARIANT(
            number_of(core::checked_mul(first, right), marker)
            == core::checked_mul(left, right)
                   .match([](std::int32_t product) { return std::int64_t{product}; }, [] { return marker; }));
        CRUCIBLE_FATAL_INVARIANT(core::wrapping_sub(first, second).raw() == core::wrapping_sub(left, right));
        CRUCIBLE_FATAL_INVARIANT(core::saturating_add(first, second).raw() == core::saturating_add(left, right));
        CRUCIBLE_FATAL_INVARIANT(core::saturating_mul(first, right).raw() == core::saturating_mul(left, right));
        CRUCIBLE_FATAL_INVARIANT(std::is_lt(first <=> second) == (left < right));
        CRUCIBLE_FATAL_INVARIANT((first == second) == (left == right));
    }
    using Bytes = core::Quantity<unit::Byte, std::uint64_t>;
    CRUCIBLE_FATAL_INVARIANT(core::checked_div(Bytes{opaque(std::uint64_t{9})}, 0).is_none());
    CRUCIBLE_FATAL_INVARIANT(core::checked_div(Bytes{opaque(std::uint64_t{9})}, 2).expect("a divisor of 2").raw() == 4);
}

}  // namespace

int main() {
    test_convert_agrees_with_the_model();
    test_convert_round_trips();
    test_arithmetic_keeps_the_unit();
    return 0;
}
