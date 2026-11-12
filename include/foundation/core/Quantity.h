#pragma once

// The Scalar family: a number with a unit.
//
//   Quantity<UnitTag, Rep>       an Integer Rep in the unit UnitTag
//   raw()                        the number
//   checked_add, checked_sub     two quantities of one unit: the exact sum or
//   wrapping_add, wrapping_sub   difference as an Option, the modular one, or
//   saturating_add, saturating_sub   the clamp
//   checked_mul, wrapping_mul, saturating_mul, checked_div
//                                a quantity and a plain number of its Rep:
//                                the unit stays
//   convert<ToUnit>(quantity)    the quantity in a different unit of the
//                                same dimension, or no value
//
// The units are the tags of namespace unit, and the gate QuantityUnit
// admits each one of them and no other type.  A unit has a dimension and a
// ratio to the base unit of its dimension:
//
//   unit::Byte          Memory   1
//   unit::Element       Count    1
//   unit::Nanosecond    Time     1
//   unit::Microsecond   Time     1,000
//   unit::Millisecond   Time     1,000,000
//   unit::Second        Time     1,000,000,000
//   unit::Cycle         Clock    1
//
// A later change adds a unit with the caller that needs it.
//
// A Quantity has no + and no -, so each overflow is a choice at the call
// site, and no default constructor, so each Quantity states its number.  Two
// quantities compare only in one unit, and the operations of two quantities
// take one unit, so a sum of bytes and elements does not compile.
//
// convert compiles only when the two units have one dimension and one ratio
// divides the other.  The same ratio gives the number with no check.  A
// finer unit multiplies by the quotient of the ratios with checked_mul, so an
// overflow gives no value.  A coarser unit gives no value when the number is
// not a whole number of the coarser unit: 1500 milliseconds is no whole
// number of seconds.  When Rep cannot hold the quotient of the ratios, 0 is
// the one number that converts.

#include <foundation/core/Arithmetic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Compare.h>
#include <foundation/core/Number.h>

#include <compare>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace unit {

// The dimensions.
struct Memory final {};
struct Count final {};
struct Time final {};
struct Clock final {};

struct Byte final {
    using dimension = Memory;
    static constexpr std::uint64_t ratio = 1;
};

struct Element final {
    using dimension = Count;
    static constexpr std::uint64_t ratio = 1;
};

struct Nanosecond final {
    using dimension = Time;
    static constexpr std::uint64_t ratio = 1;
};

struct Microsecond final {
    using dimension = Time;
    static constexpr std::uint64_t ratio = 1000;
};

struct Millisecond final {
    using dimension = Time;
    static constexpr std::uint64_t ratio = 1000000;
};

struct Second final {
    using dimension = Time;
    static constexpr std::uint64_t ratio = 1000000000;
};

struct Cycle final {
    using dimension = Clock;
    static constexpr std::uint64_t ratio = 1;
};

}  // namespace unit

// A unit of namespace unit.  The gate names each unit, so a type of a
// different header is no unit, also when it has a dimension and a ratio.
template <class U>
concept QuantityUnit =
    std::same_as<U, unit::Byte> || std::same_as<U, unit::Element> || std::same_as<U, unit::Nanosecond>
    || std::same_as<U, unit::Microsecond> || std::same_as<U, unit::Millisecond> || std::same_as<U, unit::Second>
    || std::same_as<U, unit::Cycle>;

template <QuantityUnit UnitTag, Integer Rep>
class Quantity final {
    Rep value_;

public:
    constexpr explicit Quantity(Rep value) noexcept : value_{value} {}

    [[nodiscard]] constexpr Rep raw() const noexcept { return value_; }

    [[nodiscard]] friend constexpr bool operator==(Quantity left, Quantity right) noexcept = default;
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Quantity left, Quantity right) noexcept = default;
};

namespace detail {

// The quantity of a checked number, or no value.
template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Option<Quantity<UnitTag, Rep>> quantity_of(Option<Rep>&& number) noexcept {
    using Measured = Option<Quantity<UnitTag, Rep>>;
    return static_cast<Option<Rep>&&>(number).match(
        [](Rep value) noexcept { return Measured::some(Quantity<UnitTag, Rep>{value}); },
        []() noexcept { return Measured{none}; });
}

// Two units of one dimension whose ratios divide one another.
template <class From, class To>
concept ConvertibleUnits =
    QuantityUnit<From> && QuantityUnit<To> && std::same_as<typename From::dimension, typename To::dimension>
    && (From::ratio % To::ratio == 0 || To::ratio % From::ratio == 0);

// The quantity 0 of a unit, when the quotient of two ratios does not fit in
// Rep: each other number overflows or is no whole number.
template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Option<Quantity<UnitTag, Rep>> zero_only(Rep value) noexcept {
    if (value != Rep{0}) return none;
    return Option<Quantity<UnitTag, Rep>>::some(Quantity<UnitTag, Rep>{Rep{0}});
}

}  // namespace detail

template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Option<Quantity<UnitTag, Rep>> checked_add(Quantity<UnitTag, Rep> left,
                                                                   Quantity<UnitTag, Rep> right) noexcept {
    return detail::quantity_of<UnitTag>(checked_add(left.raw(), right.raw()));
}

template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Option<Quantity<UnitTag, Rep>> checked_sub(Quantity<UnitTag, Rep> left,
                                                                   Quantity<UnitTag, Rep> right) noexcept {
    return detail::quantity_of<UnitTag>(checked_sub(left.raw(), right.raw()));
}

template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Quantity<UnitTag, Rep> wrapping_add(Quantity<UnitTag, Rep> left,
                                                            Quantity<UnitTag, Rep> right) noexcept {
    return Quantity<UnitTag, Rep>{wrapping_add(left.raw(), right.raw())};
}

template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Quantity<UnitTag, Rep> wrapping_sub(Quantity<UnitTag, Rep> left,
                                                            Quantity<UnitTag, Rep> right) noexcept {
    return Quantity<UnitTag, Rep>{wrapping_sub(left.raw(), right.raw())};
}

template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Quantity<UnitTag, Rep> saturating_add(Quantity<UnitTag, Rep> left,
                                                              Quantity<UnitTag, Rep> right) noexcept {
    return Quantity<UnitTag, Rep>{saturating_add(left.raw(), right.raw())};
}

template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Quantity<UnitTag, Rep> saturating_sub(Quantity<UnitTag, Rep> left,
                                                              Quantity<UnitTag, Rep> right) noexcept {
    return Quantity<UnitTag, Rep>{saturating_sub(left.raw(), right.raw())};
}

// The factor is a plain number of the Rep of the quantity, so a literal
// converts to it, and -Wconversion refuses a conversion that can change it.
template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Option<Quantity<UnitTag, Rep>> checked_mul(Quantity<UnitTag, Rep> quantity,
                                                                   std::type_identity_t<Rep> factor) noexcept {
    return detail::quantity_of<UnitTag>(checked_mul(quantity.raw(), factor));
}

template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Quantity<UnitTag, Rep> wrapping_mul(Quantity<UnitTag, Rep> quantity,
                                                            std::type_identity_t<Rep> factor) noexcept {
    return Quantity<UnitTag, Rep>{wrapping_mul(quantity.raw(), factor)};
}

template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Quantity<UnitTag, Rep> saturating_mul(Quantity<UnitTag, Rep> quantity,
                                                              std::type_identity_t<Rep> factor) noexcept {
    return Quantity<UnitTag, Rep>{saturating_mul(quantity.raw(), factor)};
}

// No value for a zero divisor, and for the minimum of a signed Rep divided
// by -1.
template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Option<Quantity<UnitTag, Rep>> checked_div(Quantity<UnitTag, Rep> quantity,
                                                                   std::type_identity_t<Rep> divisor) noexcept {
    return detail::quantity_of<UnitTag>(checked_div(quantity.raw(), divisor));
}

template <QuantityUnit ToUnit, class FromUnit, Integer Rep>
    requires detail::ConvertibleUnits<FromUnit, ToUnit>
[[nodiscard]] constexpr Option<Quantity<ToUnit, Rep>> convert(Quantity<FromUnit, Rep> quantity) noexcept {
    using Converted = Option<Quantity<ToUnit, Rep>>;
    Rep const value = quantity.raw();
    if constexpr (FromUnit::ratio == ToUnit::ratio) {
        return Converted::some(Quantity<ToUnit, Rep>{value});
    } else if constexpr (FromUnit::ratio > ToUnit::ratio) {
        constexpr std::uint64_t factor = FromUnit::ratio / ToUnit::ratio;
        if constexpr (fits<Rep>(factor)) {
            return detail::quantity_of<ToUnit>(checked_mul(value, static_cast<Rep>(factor)));
        } else {
            return detail::zero_only<ToUnit>(value);
        }
    } else {
        constexpr std::uint64_t factor = ToUnit::ratio / FromUnit::ratio;
        if constexpr (fits<Rep>(factor)) {
            // The quotient times the divisor gives the number back only for a
            // whole number.  The product cannot overflow, and GCC 16 builds
            // the Option of this form with no merge of its two paths, where a
            // test of the remainder costs a jump and a mask of each cell.
            constexpr Rep divisor = static_cast<Rep>(factor);
            Rep const quotient = static_cast<Rep>(value / divisor);
            if (static_cast<Rep>(quotient * divisor) != value) return none;
            return Converted::some(Quantity<ToUnit, Rep>{quotient});
        } else {
            return detail::zero_only<ToUnit>(value);
        }
    }
}

}  // namespace foundation::core
