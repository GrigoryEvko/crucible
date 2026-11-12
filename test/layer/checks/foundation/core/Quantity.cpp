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

// A type with a dimension and a ratio of its own is no unit.
struct Kibibyte {
    using dimension = unit::Memory;
    static constexpr std::uint64_t ratio = 1024;
};

// The units, and the gate.
static_assert(QuantityUnit<unit::Byte> && QuantityUnit<unit::Element> && QuantityUnit<unit::Cycle>);
static_assert(QuantityUnit<unit::Nanosecond> && QuantityUnit<unit::Microsecond> && QuantityUnit<unit::Millisecond>);
static_assert(QuantityUnit<unit::Second> && !QuantityUnit<unit::Time> && !QuantityUnit<unit::Memory>);
static_assert(!QuantityUnit<Kibibyte> && !QuantityUnit<int> && !QuantityUnit<unit::Byte const>);
static_assert(std::same_as<unit::Second::dimension, unit::Nanosecond::dimension>);
static_assert(!std::same_as<unit::Cycle::dimension, unit::Nanosecond::dimension>);

// The layout: a Quantity is its number, and an Option of a Quantity of four
// bytes is the plain Option of eight bytes.
static_assert(sizeof(Bytes) == 8 && sizeof(Quantity<unit::Element, std::uint8_t>) == 1);
static_assert(std::is_trivially_copyable_v<Bytes> && !std::is_default_constructible_v<Bytes>);
static_assert(sizeof(Option<Quantity<unit::Byte, std::uint32_t>>) == 8);
static_assert(!std::is_convertible_v<std::uint64_t, Bytes> && std::is_constructible_v<Bytes, std::uint64_t>);

// Two quantities compare only in one unit.
static_assert(Bytes{3} == Bytes{3} && std::is_lt(Bytes{3} <=> Bytes{4}) && std::is_gt(Bytes{9} <=> Bytes{4}));
static_assert(!std::equality_comparable_with<Bytes, Quantity<unit::Element, std::uint64_t>>);
static_assert(!std::equality_comparable_with<Seconds, Milliseconds>);
static_assert(!std::equality_comparable_with<Bytes, std::uint64_t>);

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
// The quotient of the ratios does not fit in a Rep of 16 bits: 0 is the one
// number that converts.
static_assert(holds_quantity(convert<unit::Nanosecond>(Quantity<unit::Second, std::int16_t>{0}), std::int16_t{0}));
static_assert(convert<unit::Nanosecond>(Quantity<unit::Second, std::int16_t>{1}).is_none());
static_assert(convert<unit::Second>(Quantity<unit::Nanosecond, std::int16_t>{30000}).is_none());
static_assert(holds_quantity(convert<unit::Second>(Quantity<unit::Nanosecond, std::int16_t>{0}), std::int16_t{0}));

// The conversion gate: one dimension, and one ratio divides the other.
template <class To, class From>
inline constexpr bool converts = requires(From quantity) { convert<To>(quantity); };

static_assert(converts<unit::Second, Nanoseconds> && converts<unit::Microsecond, Milliseconds>);
static_assert(!converts<unit::Byte, Seconds> && !converts<unit::Cycle, Nanoseconds>);
static_assert(!converts<unit::Element, Bytes> && !converts<Kibibyte, Bytes>);

}  // namespace foundation::core::detail::quantity_checks
