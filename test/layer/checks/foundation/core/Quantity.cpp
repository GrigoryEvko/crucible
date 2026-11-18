// The compile-time checks of foundation/core/Quantity.h.

#include <foundation/core/Quantity.h>

#include <compare>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace foundation::core::detail::quantity_checks {

using Bytes = Quantity<unit::Byte, std::uint64_t>;
using Seconds = Quantity<unit::Second, std::int64_t>;
using Milliseconds = Quantity<unit::Millisecond, std::int64_t>;
using Nanoseconds = Quantity<unit::Nanosecond, std::int64_t>;

// A class with the dimension and the scale of a unit of its own is no unit.
struct Kibibyte {
    static constexpr unit::Dimension dimension{.memory = 1};
    static constexpr unit::Scale scale{.power = 1};
};

// A class with the dimension and the scale of a named unit is no unit.
struct OtherByte {
    static constexpr unit::Dimension dimension{.memory = 1};
    static constexpr unit::Scale scale{};
};

// The units, and the gate.
static_assert(QuantityUnit<unit::Byte> && QuantityUnit<unit::Element> && QuantityUnit<unit::Cycle>);
static_assert(QuantityUnit<unit::Nanosecond> && QuantityUnit<unit::Microsecond> && QuantityUnit<unit::Millisecond>);
static_assert(QuantityUnit<unit::Second> && QuantityUnit<unit::Flop> && QuantityUnit<unit::BytePerSecond>);
static_assert(QuantityUnit<unit::FlopPerSecond> && QuantityUnit<unit::FlopPerByte>);
static_assert(!QuantityUnit<Kibibyte> && !QuantityUnit<OtherByte> && !QuantityUnit<int>);
static_assert(!QuantityUnit<unit::Byte const> && !QuantityUnit<unit::Dimension> && !QuantityUnit<unit::Scale>);
static_assert(unit::Second::dimension == unit::Nanosecond::dimension);
static_assert(!(unit::Cycle::dimension == unit::Nanosecond::dimension));
static_assert(!(unit::Second::scale == unit::Millisecond::scale));

// A Derived unit with no name is a unit.  A Derived unit with the dimension
// and the scale of a named unit is no unit, so each unit has one spelling.
using SquareByte = unit::Derived<unit::Dimension{.memory = 2}, unit::Scale{}>;
static_assert(QuantityUnit<SquareByte>);
static_assert(!QuantityUnit<unit::Derived<unit::Byte::dimension, unit::Byte::scale>>);
static_assert(!QuantityUnit<unit::Derived<unit::Second::dimension, unit::Second::scale>>);

// A product adds the exponents and the powers, and a quotient subtracts
// them.  A result with the dimension and the scale of a named unit is that
// named unit.
static_assert(std::same_as<unit::Quotient<unit::Byte, unit::Second>, unit::BytePerSecond>);
static_assert(std::same_as<unit::Quotient<unit::Flop, unit::Second>, unit::FlopPerSecond>);
static_assert(std::same_as<unit::Product<unit::BytePerSecond, unit::Second>, unit::Byte>);
static_assert(std::same_as<unit::Product<unit::Second, unit::BytePerSecond>, unit::Byte>);
static_assert(std::same_as<unit::Quotient<unit::FlopPerSecond, unit::BytePerSecond>, unit::FlopPerByte>);
static_assert(std::same_as<unit::Product<unit::Byte, unit::Byte>, SquareByte>);
static_assert(std::same_as<unit::Quotient<SquareByte, unit::Byte>, unit::Byte>);
static_assert(std::same_as<unit::Product<unit::Second, unit::Millisecond>,
                           unit::Derived<unit::Dimension{.time = 2}, unit::Scale{.power = 5}>>);
// A frequency is cycles per second.  Cycles over a frequency are seconds,
// and no convert goes from cycles to seconds.
using CyclePerSecond = unit::Quotient<unit::Cycle, unit::Second>;
static_assert(
    std::same_as<CyclePerSecond, unit::Derived<unit::Dimension{.time = -1, .clock = 1}, unit::Scale{.power = -3}>>);
static_assert(std::same_as<unit::Quotient<unit::Cycle, CyclePerSecond>, unit::Second>);
// A quotient of two units of one dimension has no dimension, and keeps the
// difference of the scales.
static_assert(std::same_as<unit::Quotient<unit::Byte, unit::Byte>, unit::Derived<unit::Dimension{}, unit::Scale{}>>);
static_assert(std::same_as<unit::Quotient<unit::Second, unit::Millisecond>,
                           unit::Derived<unit::Dimension{}, unit::Scale{.power = 1}>>);

// An exponent or a power that does not fit in 8 bits has no unit.
template <class Left, class Right>
inline constexpr bool has_product = requires { typename unit::Product<Left, Right>; };
template <class Left, class Right>
inline constexpr bool has_quotient = requires { typename unit::Quotient<Left, Right>; };
using HighestMemory = unit::Derived<unit::Dimension{.memory = 127}, unit::Scale{}>;
using LowestMemory = unit::Derived<unit::Dimension{.memory = -128}, unit::Scale{}>;
using LowestPower = unit::Derived<unit::Dimension{.count = 1}, unit::Scale{.power = -128}>;
static_assert(has_product<HighestMemory, unit::Element> && !has_product<HighestMemory, unit::Byte>);
static_assert(has_quotient<HighestMemory, unit::Byte> && has_quotient<unit::Element, HighestMemory>);
static_assert(!has_quotient<LowestMemory, unit::Byte> && !has_quotient<unit::Byte, LowestMemory>);
static_assert(has_product<LowestPower, unit::Microsecond> && !has_quotient<LowestPower, unit::Microsecond>);
static_assert(!has_product<int, unit::Byte> && !has_quotient<unit::Byte, OtherByte>);

// The layout: a Quantity is its number, and an Option of a Quantity of four
// bytes is the plain Option of eight bytes.
static_assert(sizeof(Bytes) == 8 && sizeof(Quantity<unit::Element, std::uint8_t>) == 1);
static_assert(std::is_trivially_copyable_v<Bytes> && !std::is_default_constructible_v<Bytes>);
static_assert(sizeof(Option<Quantity<unit::Byte, std::uint32_t>>) == 8);
static_assert(!std::is_convertible_v<std::uint64_t, Bytes> && std::is_constructible_v<Bytes, std::uint64_t>);
static_assert(sizeof(Quantity<unit::BytePerSecond, std::uint64_t>) == 8);

// Two quantities compare only in one unit.
static_assert(Bytes{3} == Bytes{3} && std::is_lt(Bytes{3} <=> Bytes{4}) && std::is_gt(Bytes{9} <=> Bytes{4}));
static_assert(std::same_as<decltype(Bytes{3} <=> Bytes{4}), std::strong_ordering>);
static_assert(!std::equality_comparable_with<Bytes, Quantity<unit::Element, std::uint64_t>>);
static_assert(!std::equality_comparable_with<Seconds, Milliseconds>);
static_assert(!std::equality_comparable_with<Bytes, std::uint64_t>);
using ByteRate = Quantity<unit::BytePerSecond, std::uint64_t>;
using FlopRate = Quantity<unit::FlopPerSecond, std::uint64_t>;
static_assert(ByteRate{5} == ByteRate{5} && std::is_lt(ByteRate{4} <=> ByteRate{5}));
static_assert(!std::equality_comparable_with<ByteRate, FlopRate>);
template <class Left, class Right>
inline constexpr bool orders = requires(Left left, Right right) { left < right; };
static_assert(orders<ByteRate, ByteRate> && !orders<ByteRate, FlopRate> && !orders<ByteRate, std::uint64_t>);
static_assert(ByteRate{4} < ByteRate{5} && ByteRate{5} <= ByteRate{5} && ByteRate{6} > ByteRate{5});
static_assert(ByteRate{5} >= ByteRate{5} && !(ByteRate{5} < ByteRate{5}) && !(ByteRate{4} > ByteRate{5}));

template <class UnitTag, class Rep>
[[nodiscard]] consteval bool holds_quantity(Option<Quantity<UnitTag, Rep>> result, Rep expected) {
    return result.match([expected](Quantity<UnitTag, Rep> value) { return value.raw() == expected; },
                        [] { return false; });
}

// The arithmetic keeps the unit.
static_assert(holds_quantity(checked_add(Bytes{3}, Bytes{4}), std::uint64_t{7}));
static_assert(checked_add(Bytes{max_of<std::uint64_t>}, Bytes{1}).is_none());
static_assert(checked_sub(Bytes{3}, Bytes{4}).is_none()
              && wrapping_sub(Bytes{3}, Bytes{4}).raw() == max_of<std::uint64_t>);
static_assert(saturating_add(Bytes{max_of<std::uint64_t>}, Bytes{1}).raw() == max_of<std::uint64_t>);
static_assert(saturating_sub(Bytes{3}, Bytes{4}).raw() == 0 && wrapping_add(Bytes{2}, Bytes{5}).raw() == 7);
static_assert(holds_quantity(checked_mul(Bytes{3}, 4), std::uint64_t{12}) && wrapping_mul(Bytes{3}, 4).raw() == 12);
static_assert(saturating_mul(Bytes{max_of<std::uint64_t>}, 2).raw() == max_of<std::uint64_t>);
static_assert(holds_quantity(checked_div(Seconds{9}, 2), std::int64_t{4}) && checked_div(Seconds{9}, 0).is_none());
static_assert(checked_div(Seconds{min_of<std::int64_t>}, -1).is_none());
static_assert(std::same_as<decltype(checked_add(Bytes{1}, Bytes{2})), Option<Bytes>>);

// The product and the quotient of two quantities have the product and the
// quotient of their units.
using RateSeconds = Quantity<unit::Second, std::uint64_t>;
static_assert(std::same_as<decltype(checked_mul(ByteRate{3}, RateSeconds{4})), Option<Bytes>>);
static_assert(holds_quantity(checked_mul(ByteRate{3}, RateSeconds{4}), std::uint64_t{12}));
static_assert(holds_quantity(checked_mul(RateSeconds{4}, ByteRate{3}), std::uint64_t{12}));
static_assert(checked_mul(ByteRate{max_of<std::uint64_t>}, RateSeconds{2}).is_none());
static_assert(std::same_as<decltype(wrapping_mul(ByteRate{3}, RateSeconds{4})), Bytes>);
static_assert(wrapping_mul(ByteRate{max_of<std::uint64_t>}, RateSeconds{2}).raw() == max_of < std::uint64_t > -1);
static_assert(saturating_mul(ByteRate{max_of<std::uint64_t>}, RateSeconds{2}).raw() == max_of<std::uint64_t>);
static_assert(std::same_as<decltype(checked_div(Bytes{9}, RateSeconds{2})), Option<ByteRate>>);
static_assert(holds_quantity(checked_div(Bytes{9}, RateSeconds{2}), std::uint64_t{4}));
static_assert(holds_quantity(checked_div(Bytes{12}, ByteRate{4}), std::uint64_t{3}));
static_assert(std::same_as<decltype(checked_div(Bytes{12}, ByteRate{4})), Option<RateSeconds>>);
static_assert(checked_div(Bytes{9}, RateSeconds{0}).is_none());
static_assert(checked_div(Seconds{min_of<std::int64_t>}, Quantity<unit::Element, std::int64_t>{-1}).is_none());
// The sum of a product and a quantity of its unit compiles.  The sum with
// seconds does not: neg_core_scalar_quantity_product_plus_seconds.
static_assert(holds_quantity(checked_add(checked_mul(ByteRate{3}, RateSeconds{4}).expect("a small product"), Bytes{1}),
                             std::uint64_t{13}));
// Two quantities of different Reps meet in no operation.
template <class Left, class Right>
inline constexpr bool multiplies = requires(Left left, Right right) { checked_mul(left, right); };
static_assert(multiplies<ByteRate, RateSeconds> && !multiplies<ByteRate, Seconds>);

// convert: the same unit with no check, a finer unit with checked_mul, and
// a coarser unit only for a whole number.
static_assert(holds_quantity(convert<unit::Second>(Seconds{3}), std::int64_t{3}));
static_assert(holds_quantity(convert<unit::Nanosecond>(Seconds{3}), std::int64_t{3000000000}));
static_assert(holds_quantity(convert<unit::Millisecond>(Seconds{-2}), std::int64_t{-2000}));
static_assert(holds_quantity(convert<unit::Second>(Milliseconds{-2000}), std::int64_t{-2}));
static_assert(convert<unit::Second>(Milliseconds{1500}).is_none());
static_assert(convert<unit::Second>(Milliseconds{-1500}).is_none());
static_assert(convert<unit::Nanosecond>(Seconds{max_of<std::int64_t> / 1000000000 + 1}).is_none());
static_assert(holds_quantity(convert<unit::Nanosecond>(Quantity<unit::Second, std::uint32_t>{4}), 4000000000u));
static_assert(convert<unit::Nanosecond>(Quantity<unit::Second, std::int32_t>{3}).is_none());
// The factor does not fit in a Rep of 16 bits: 0 is the one number that
// converts.
static_assert(holds_quantity(convert<unit::Nanosecond>(Quantity<unit::Second, std::int16_t>{0}), std::int16_t{0}));
static_assert(convert<unit::Nanosecond>(Quantity<unit::Second, std::int16_t>{1}).is_none());
static_assert(convert<unit::Second>(Quantity<unit::Nanosecond, std::int16_t>{30000}).is_none());
static_assert(holds_quantity(convert<unit::Second>(Quantity<unit::Nanosecond, std::int16_t>{0}), std::int16_t{0}));
// A rate converts as its scale: 3 * 10^9 bytes per second is 3 bytes per
// nanosecond.
using BytePerNanosecond = unit::Quotient<unit::Byte, unit::Nanosecond>;
static_assert(holds_quantity(convert<BytePerNanosecond>(ByteRate{3000000000}), std::uint64_t{3}));
static_assert(convert<BytePerNanosecond>(ByteRate{3000000001}).is_none());
static_assert(holds_quantity(convert<unit::BytePerSecond>(Quantity<BytePerNanosecond, std::uint64_t>{2}),
                             std::uint64_t{2000000000}));
// 1000^6 fits in 64 bits, and 1000^7 does not: then 0 is the one number
// that converts.
using Thousand = unit::Quotient<unit::Microsecond, unit::Nanosecond>;
using Gigasecond = unit::Product<unit::Second, unit::Quotient<unit::Second, unit::Nanosecond>>;
using Terasecond = unit::Product<Gigasecond, Thousand>;
static_assert(holds_quantity(convert<unit::Nanosecond>(Quantity<Gigasecond, std::uint64_t>{1}),
                             std::uint64_t{1000000000000000000}));
static_assert(holds_quantity(convert<unit::Nanosecond>(Quantity<Terasecond, std::uint64_t>{0}), std::uint64_t{0}));
static_assert(convert<unit::Nanosecond>(Quantity<Terasecond, std::uint64_t>{1}).is_none());
static_assert(convert<Terasecond>(Quantity<unit::Nanosecond, std::uint64_t>{max_of<std::uint64_t>}).is_none());

// The conversion gate: one dimension.
template <class To, class From>
inline constexpr bool converts = requires(From quantity) { convert<To>(quantity); };

static_assert(converts<unit::Second, Nanoseconds> && converts<unit::Microsecond, Milliseconds>);
static_assert(!converts<unit::Byte, Seconds> && !converts<unit::Cycle, Nanoseconds>);
static_assert(!converts<unit::Element, Bytes> && !converts<Kibibyte, Bytes>);
static_assert(!converts<unit::FlopPerSecond, ByteRate>
              && !converts<unit::Second, Quantity<unit::Cycle, std::uint64_t>>);

// The number of a float quantity has the bits of the expected number.  The
// key of the total order is one key for each bit pattern.
template <class UnitTag, class Rep>
[[nodiscard]] consteval bool has_bits(Quantity<UnitTag, Rep> quantity, Rep expected) {
    return fp::total_key(quantity.raw()) == fp::total_key(expected);
}

// The integer to float door: one IEEE conversion, and the unit stays.
static_assert(std::same_as<decltype(round_to_float<double>(Bytes{3})), Quantity<unit::Byte, double>>);
static_assert(has_bits(round_to_float<double>(Bytes{3}), 3.0));
static_assert(has_bits(round_to_float<float>(Bytes{(std::uint64_t{1} << 24) + 1}), 16777216.0f));

// ── The float set ───────────────────────────────────────────────────

using FloatBytes = Quantity<unit::Byte, double>;
using FloatSeconds = Quantity<unit::Second, double>;
using FloatByteRate = Quantity<unit::BytePerSecond, double>;
using FloatFlopRate = Quantity<unit::FlopPerSecond, double>;

static_assert(sizeof(FloatBytes) == 8 && std::is_trivially_copyable_v<FloatBytes>);
static_assert(!std::is_default_constructible_v<FloatBytes> && !std::is_convertible_v<double, FloatBytes>);
static_assert(std::same_as<decltype(FloatBytes{1.0} <=> FloatBytes{2.0}), std::partial_ordering>);
static_assert(FloatBytes{1.5} == FloatBytes{1.5} && FloatBytes{1.0} < FloatBytes{2.0});
static_assert(!std::equality_comparable_with<FloatByteRate, FloatFlopRate>);
static_assert(!std::equality_comparable_with<FloatBytes, Bytes>);

// Each operation is one IEEE operation.
static_assert(has_bits(add(FloatBytes{1.5}, FloatBytes{2.25}), 3.75));
static_assert(has_bits(subtract(FloatBytes{1.5}, FloatBytes{2.25}), -0.75));
static_assert(has_bits(multiply(FloatBytes{1.5}, 4.0), 6.0) && has_bits(divide(FloatBytes{1.5}, 4.0), 0.375));
static_assert(std::same_as<decltype(multiply(FloatByteRate{2.0}, FloatSeconds{3.0})), FloatBytes>);
static_assert(has_bits(multiply(FloatByteRate{2.0}, FloatSeconds{3.0}), 6.0));
static_assert(std::same_as<decltype(divide(FloatBytes{6.0}, FloatByteRate{2.0})), FloatSeconds>);
static_assert(has_bits(divide(FloatBytes{6.0}, FloatByteRate{2.0}), 3.0));
// The ridge point of a part of 1.125 * 10^15 flops per second and 8 * 10^12
// bytes per second is 140.625 flops per byte, exactly.
static_assert(std::same_as<decltype(divide(FloatFlopRate{1.125e15}, FloatByteRate{8.0e12})),
                           Quantity<unit::FlopPerByte, double>>);
static_assert(has_bits(divide(FloatFlopRate{1.125e15}, FloatByteRate{8.0e12}), 140.625));
// The sign of a zero stays.  test_core_scalar_quantity holds the NaN cases
// of an operation, because a NaN result is no constant expression under
// -frounding-math.
static_assert(has_bits(subtract(FloatBytes{-0.0}, FloatBytes{0.0}), -0.0));

// A zero divisor gives the IEEE quotient with no division: the infinity with
// the sign of the quotient, or the canonical NaN for 0 / 0.
static_assert(has_bits(divide(FloatBytes{1.5}, 0.0), infinity_of<double>));
static_assert(has_bits(divide(FloatBytes{-1.5}, 0.0), -infinity_of<double>));
static_assert(has_bits(divide(FloatBytes{1.5}, -0.0), -infinity_of<double>));
static_assert(has_bits(divide(FloatBytes{-1.5}, -0.0), infinity_of<double>));
static_assert(has_bits(divide(FloatBytes{infinity_of<double>}, 0.0), infinity_of<double>));
static_assert(has_bits(divide(FloatBytes{0.0}, 0.0), quiet_nan_of<double>));
static_assert(has_bits(divide(FloatBytes{-0.0}, -0.0), quiet_nan_of<double>));
static_assert(has_bits(divide(FloatBytes{6.0}, FloatByteRate{0.0}), infinity_of<double>));
static_assert(has_bits(divide(Quantity<unit::Byte, float>{2.0f}, -0.0f), -infinity_of<float>));

// convert of a Float Rep: one multiply for a finer unit, one divide for a
// coarser unit, and only when the Rep holds the factor exactly.
static_assert(std::same_as<decltype(convert<unit::Nanosecond>(FloatSeconds{1.5})), Quantity<unit::Nanosecond, double>>);
static_assert(has_bits(convert<unit::Nanosecond>(FloatSeconds{1.5}), 1.5e9));
static_assert(has_bits(convert<unit::Second>(Quantity<unit::Millisecond, double>{1500.0}), 1.5));
static_assert(has_bits(convert<BytePerNanosecond>(FloatByteRate{8.0e12}), 8000.0));
static_assert(has_bits(convert<unit::Second>(FloatSeconds{2.5}), 2.5));
// A float holds 1000^3 exactly and not 1000^4.  A double holds 1000^6
// exactly, and 1000^7 does not fit in 64 bits.
static_assert(converts<unit::Nanosecond, Quantity<unit::Second, float>>);
static_assert(converts<unit::Nanosecond, Quantity<unit::Millisecond, float>>);
static_assert(converts<unit::Second, Quantity<Gigasecond, float>>);
static_assert(!converts<unit::Millisecond, Quantity<Gigasecond, float>>);
static_assert(converts<unit::Nanosecond, Quantity<Gigasecond, double>>);
static_assert(!converts<unit::Nanosecond, Quantity<Terasecond, double>>);
static_assert(!converts<BytePerNanosecond, Quantity<unit::Byte, float>>);

// The integer set takes no float quantity, the float set takes no integer
// quantity, and no operation takes the two together.
template <class Left, class Right>
inline constexpr bool adds_checked = requires(Left left, Right right) { checked_add(left, right); };
template <class Left, class Right>
inline constexpr bool adds = requires(Left left, Right right) { add(left, right); };
template <class Left, class Right>
inline constexpr bool divides = requires(Left left, Right right) { divide(left, right); };
static_assert(adds_checked<Bytes, Bytes> && !adds_checked<FloatBytes, FloatBytes>);
static_assert(adds<FloatBytes, FloatBytes> && !adds<Bytes, Bytes> && !adds<FloatBytes, Bytes>);
static_assert(!adds_checked<FloatBytes, Bytes> && !adds_checked<Bytes, FloatBytes>);
static_assert(divides<FloatBytes, FloatByteRate> && !divides<FloatBytes, ByteRate>);
static_assert(!multiplies<FloatByteRate, FloatSeconds> && !multiplies<ByteRate, FloatSeconds>);

}  // namespace foundation::core::detail::quantity_checks
