#pragma once

// The Scalar family: a number with a unit.
//
//   Quantity<UnitTag, Rep>       an Integer Rep in the unit UnitTag
//   raw()                        the number
//   unit::Product<Left, Right>   the unit of a product of two quantities
//   unit::Quotient<Left, Right>  the unit of a quotient of two quantities
//   convert<ToUnit>(quantity)    the quantity in a different unit of the
//                                same dimension
//
// The integer set:
//
//   checked_add, checked_sub     two quantities of one unit: the exact sum or
//   wrapping_add, wrapping_sub   difference as an Option, the modular one, or
//   saturating_add, saturating_sub   the clamp
//   checked_mul, wrapping_mul, saturating_mul, checked_div
//                                a quantity and a plain number of its Rep,
//                                with the same unit, or two quantities, with
//                                the product or the quotient of their units
//
// UNITS.  A unit has a dimension and a scale.  The dimension is a vector of
// five integer exponents, one for each base dimension:
//
//   memory   bytes                time    nanoseconds
//   count    elements             work    flops (floating-point operations)
//   clock    cycles
//
// The scale is a power of 1000: the unit holds 1000^power base units of its
// dimension.  A product adds the exponents and the powers, and a quotient
// subtracts them, at compile time.  Bytes per second has the exponents
// memory 1 and time -1, and the power -3.
//
// These units have names in namespace unit:
//
//   unit::Byte            memory 1              power 0
//   unit::Element         count 1               power 0
//   unit::Nanosecond      time 1                power 0
//   unit::Microsecond     time 1                power 1
//   unit::Millisecond     time 1                power 2
//   unit::Second          time 1                power 3
//   unit::Cycle           clock 1               power 0
//   unit::Flop            work 1                power 0
//   unit::BytePerSecond   memory 1, time -1     power -3
//   unit::FlopPerSecond   time -1, work 1       power -3
//   unit::FlopPerByte     memory -1, work 1     power 0
//
// A product or a quotient with the dimension and the scale of a named unit
// is that named unit, so bytes per second times seconds is unit::Byte.  Each
// other product or quotient is a unit::Derived<Dimension, Scale>.  The gate
// QuantityUnit admits each named unit and each Derived unit with no name, and
// no other type.  Thus the units are the products and the quotients of the
// named units, and a class of a different header with a dimension and a
// scale is no unit.  A later change adds a named unit with the caller that
// needs it.  When an exponent or the power of a product does not fit in 8
// bits, the product has no unit, and the operation does not compile.
//
// A Quantity has no operator other than the compare, and no default
// constructor, so each Quantity states its number.  Two quantities compare
// only in one unit and with one Rep.  Thus a compare of bytes per second
// with flops per second does not compile.
//
// THE INTEGER SET.  It has no + and no -, so each overflow is a choice at the
// call site.  The operations of two quantities take one Rep, and the sum and
// the difference take one unit.  Thus a sum of bytes and seconds does not
// compile.  convert compiles only for two units of one dimension, and they
// differ by a factor 1000^k.  The same scale gives the number with no check.
// A finer unit multiplies by 1000^k with checked_mul, so an overflow gives no
// value.  A coarser unit gives no value when the number is not a whole
// number of the coarser unit: 1500 milliseconds is no whole number of
// seconds.  When Rep cannot hold 1000^k, 0 is the one number that converts.

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

// The exponent of each base dimension in a unit.
struct Dimension final {
    std::int8_t memory{};
    std::int8_t count{};
    std::int8_t time{};
    std::int8_t work{};
    std::int8_t clock{};

    [[nodiscard]] friend constexpr bool operator==(Dimension left, Dimension right) noexcept = default;
};

// The unit holds 1000^power base units of its dimension.
struct Scale final {
    std::int8_t power{};

    [[nodiscard]] friend constexpr bool operator==(Scale left, Scale right) noexcept = default;
};

// A product or a quotient of units that has no name.
template <Dimension Exponents, Scale Size>
struct Derived final {
    static constexpr Dimension dimension = Exponents;
    static constexpr Scale scale = Size;
};

struct Byte final {
    static constexpr Dimension dimension{.memory = 1};
    static constexpr Scale scale{};
};

struct Element final {
    static constexpr Dimension dimension{.count = 1};
    static constexpr Scale scale{};
};

struct Nanosecond final {
    static constexpr Dimension dimension{.time = 1};
    static constexpr Scale scale{};
};

struct Microsecond final {
    static constexpr Dimension dimension{.time = 1};
    static constexpr Scale scale{.power = 1};
};

struct Millisecond final {
    static constexpr Dimension dimension{.time = 1};
    static constexpr Scale scale{.power = 2};
};

struct Second final {
    static constexpr Dimension dimension{.time = 1};
    static constexpr Scale scale{.power = 3};
};

struct Cycle final {
    static constexpr Dimension dimension{.clock = 1};
    static constexpr Scale scale{};
};

struct Flop final {
    static constexpr Dimension dimension{.work = 1};
    static constexpr Scale scale{};
};

struct BytePerSecond final {
    static constexpr Dimension dimension{.memory = 1, .time = -1};
    static constexpr Scale scale{.power = -3};
};

struct FlopPerSecond final {
    static constexpr Dimension dimension{.time = -1, .work = 1};
    static constexpr Scale scale{.power = -3};
};

struct FlopPerByte final {
    static constexpr Dimension dimension{.memory = -1, .work = 1};
    static constexpr Scale scale{};
};

}  // namespace unit

namespace detail {

// The named unit of a dimension and a scale, or the Derived unit when no
// named unit has them.  This table is the one list of the named units.
template <unit::Dimension Exponents, unit::Scale Size>
struct NamedUnit final {
    using type = unit::Derived<Exponents, Size>;
};

template <>
struct NamedUnit<unit::Byte::dimension, unit::Byte::scale> final {
    using type = unit::Byte;
};

template <>
struct NamedUnit<unit::Element::dimension, unit::Element::scale> final {
    using type = unit::Element;
};

template <>
struct NamedUnit<unit::Nanosecond::dimension, unit::Nanosecond::scale> final {
    using type = unit::Nanosecond;
};

template <>
struct NamedUnit<unit::Microsecond::dimension, unit::Microsecond::scale> final {
    using type = unit::Microsecond;
};

template <>
struct NamedUnit<unit::Millisecond::dimension, unit::Millisecond::scale> final {
    using type = unit::Millisecond;
};

template <>
struct NamedUnit<unit::Second::dimension, unit::Second::scale> final {
    using type = unit::Second;
};

template <>
struct NamedUnit<unit::Cycle::dimension, unit::Cycle::scale> final {
    using type = unit::Cycle;
};

template <>
struct NamedUnit<unit::Flop::dimension, unit::Flop::scale> final {
    using type = unit::Flop;
};

template <>
struct NamedUnit<unit::BytePerSecond::dimension, unit::BytePerSecond::scale> final {
    using type = unit::BytePerSecond;
};

template <>
struct NamedUnit<unit::FlopPerSecond::dimension, unit::FlopPerSecond::scale> final {
    using type = unit::FlopPerSecond;
};

template <>
struct NamedUnit<unit::FlopPerByte::dimension, unit::FlopPerByte::scale> final {
    using type = unit::FlopPerByte;
};

template <unit::Dimension Exponents, unit::Scale Size>
using named_unit_t = typename NamedUnit<Exponents, Size>::type;

// A class with a Dimension and a Scale as its static members.
template <class U>
concept HasUnitShape = requires {
    { U::dimension } -> std::same_as<unit::Dimension const&>;
    { U::scale } -> std::same_as<unit::Scale const&>;
};

// The table gives the class itself for its dimension and its scale.
template <class U>
concept IsTabledUnit = std::same_as<named_unit_t<U::dimension, U::scale>, U>;

}  // namespace detail

// A unit of namespace unit: a named unit, or a Derived unit with no name.
template <class U>
concept QuantityUnit = detail::HasUnitShape<U> && detail::IsTabledUnit<U>;

namespace detail {

enum class UnitOperation : std::uint8_t {
    Product = 0,
    Quotient = 1,
};

[[nodiscard]] consteval bool fits_exponent(int value) noexcept {
    return value >= int{min_of<std::int8_t>} && value <= int{max_of<std::int8_t>};
}

// The exponents and the power of a product or a quotient of two units, in
// int, so the sum of two exponents of 8 bits has no overflow.
template <class Left, class Right, UnitOperation Operation>
struct CombinedUnit final {
    static constexpr int sign = Operation == UnitOperation::Product ? 1 : -1;
    static constexpr int memory = int{Left::dimension.memory} + sign * int{Right::dimension.memory};
    static constexpr int count = int{Left::dimension.count} + sign * int{Right::dimension.count};
    static constexpr int time = int{Left::dimension.time} + sign * int{Right::dimension.time};
    static constexpr int work = int{Left::dimension.work} + sign * int{Right::dimension.work};
    static constexpr int clock = int{Left::dimension.clock} + sign * int{Right::dimension.clock};
    static constexpr int power = int{Left::scale.power} + sign * int{Right::scale.power};
    static constexpr bool is_representable = fits_exponent(memory) && fits_exponent(count) && fits_exponent(time)
                                          && fits_exponent(work) && fits_exponent(clock) && fits_exponent(power);
    // The cast wraps only when is_representable is false, and the gate of
    // Product and Quotient then refuses the unit.
    using type = named_unit_t<unit::Dimension{.memory = static_cast<std::int8_t>(memory),
                                              .count = static_cast<std::int8_t>(count),
                                              .time = static_cast<std::int8_t>(time),
                                              .work = static_cast<std::int8_t>(work),
                                              .clock = static_cast<std::int8_t>(clock)},
                              unit::Scale{.power = static_cast<std::int8_t>(power)}>;
};

}  // namespace detail

namespace unit {

template <QuantityUnit Left, QuantityUnit Right>
    requires detail::CombinedUnit<Left, Right, detail::UnitOperation::Product>::is_representable
using Product = typename detail::CombinedUnit<Left, Right, detail::UnitOperation::Product>::type;

template <QuantityUnit Left, QuantityUnit Right>
    requires detail::CombinedUnit<Left, Right, detail::UnitOperation::Quotient>::is_representable
using Quotient = typename detail::CombinedUnit<Left, Right, detail::UnitOperation::Quotient>::type;

}  // namespace unit

template <QuantityUnit UnitTag, Integer Rep>
class Quantity final {
    Rep value_{};

public:
    constexpr explicit Quantity(Rep value) noexcept : value_{value} {}

    [[nodiscard]] constexpr Rep raw() const noexcept { return value_; }

    [[nodiscard]] friend constexpr bool operator==(Quantity left, Quantity right) noexcept = default;
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Quantity left, Quantity right) noexcept = default;

    // The four orders compare the numbers, so a caller reads no ordering
    // type of the standard library.
    [[nodiscard]] friend constexpr bool operator<(Quantity left, Quantity right) noexcept {
        return left.value_ < right.value_;
    }
    [[nodiscard]] friend constexpr bool operator<=(Quantity left, Quantity right) noexcept {
        return left.value_ <= right.value_;
    }
    [[nodiscard]] friend constexpr bool operator>(Quantity left, Quantity right) noexcept {
        return left.value_ > right.value_;
    }
    [[nodiscard]] friend constexpr bool operator>=(Quantity left, Quantity right) noexcept {
        return left.value_ >= right.value_;
    }
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

// Two units of one dimension.  Their scales differ by a power of 1000.
template <class From, class To>
concept ConvertibleUnits = QuantityUnit<From> && QuantityUnit<To> && (From::dimension == To::dimension);

// The steps of 1000 from the scale of From to the scale of To: positive when
// To is the finer unit.
template <class From, class To>
inline constexpr int scale_steps = int{From::scale.power} - int{To::scale.power};

// 1000^6 is the highest power of 1000 that 64 bits hold.
inline constexpr int kWidestScaleStep = 6;

// 1000^steps, for 0 <= steps <= kWidestScaleStep.
[[nodiscard]] consteval std::uint64_t power_of_1000(int steps) noexcept {
    std::uint64_t factor = 1;
    for (int step = 0; step < steps; ++step)
        factor *= 1000;
    return factor;
}

// The quantity 0 of a unit, when Rep cannot hold the factor 1000^k: each
// other number overflows or is no whole number.
template <class UnitTag, Integer Rep>
[[nodiscard]] constexpr Option<Quantity<UnitTag, Rep>> zero_only(Rep value) noexcept {
    if (value != Rep{0}) return none;
    return Option<Quantity<UnitTag, Rep>>::some(Quantity<UnitTag, Rep>{Rep{0}});
}

}  // namespace detail

// ── The integer set ─────────────────────────────────────────────────

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

// The product of two quantities has the product of their units.  The
// arguments swap with no change of the number, and the unit is the same up
// to the order of the factors.
template <class LeftUnit, class RightUnit, Integer Rep>
[[nodiscard]] constexpr Option<Quantity<unit::Product<LeftUnit, RightUnit>, Rep>>
checked_mul(Quantity<LeftUnit, Rep> left, Quantity<RightUnit, Rep> right) noexcept {
    return detail::quantity_of<unit::Product<LeftUnit, RightUnit>>(checked_mul(left.raw(), right.raw()));
}

template <class LeftUnit, class RightUnit, Integer Rep>
[[nodiscard]] constexpr Quantity<unit::Product<LeftUnit, RightUnit>, Rep>
wrapping_mul(Quantity<LeftUnit, Rep> left, Quantity<RightUnit, Rep> right) noexcept {
    return Quantity<unit::Product<LeftUnit, RightUnit>, Rep>{wrapping_mul(left.raw(), right.raw())};
}

template <class LeftUnit, class RightUnit, Integer Rep>
[[nodiscard]] constexpr Quantity<unit::Product<LeftUnit, RightUnit>, Rep>
saturating_mul(Quantity<LeftUnit, Rep> left, Quantity<RightUnit, Rep> right) noexcept {
    return Quantity<unit::Product<LeftUnit, RightUnit>, Rep>{saturating_mul(left.raw(), right.raw())};
}

// The quotient of two quantities has the quotient of their units.  The
// number rounds toward zero, as the division of Rep.  No value for a zero
// divisor, and for the minimum of a signed Rep divided by -1.
template <class DividendUnit, class DivisorUnit, Integer Rep>
[[nodiscard]] constexpr Option<Quantity<unit::Quotient<DividendUnit, DivisorUnit>, Rep>>
checked_div(Quantity<DividendUnit, Rep> dividend, Quantity<DivisorUnit, Rep> divisor) noexcept {
    return detail::quantity_of<unit::Quotient<DividendUnit, DivisorUnit>>(checked_div(dividend.raw(), divisor.raw()));
}

template <QuantityUnit ToUnit, class FromUnit, Integer Rep>
    requires detail::ConvertibleUnits<FromUnit, ToUnit>
[[nodiscard]] constexpr Option<Quantity<ToUnit, Rep>> convert(Quantity<FromUnit, Rep> quantity) noexcept {
    using Converted = Option<Quantity<ToUnit, Rep>>;
    constexpr int steps = detail::scale_steps<FromUnit, ToUnit>;
    constexpr int magnitude = steps < 0 ? -steps : steps;
    Rep const value = quantity.raw();
    if constexpr (steps == 0) {
        return Converted::some(Quantity<ToUnit, Rep>{value});
    } else if constexpr (magnitude > detail::kWidestScaleStep) {
        return detail::zero_only<ToUnit>(value);
    } else {
        constexpr std::uint64_t factor = detail::power_of_1000(magnitude);
        if constexpr (!fits<Rep>(factor)) {
            return detail::zero_only<ToUnit>(value);
        } else if constexpr (steps > 0) {
            return detail::quantity_of<ToUnit>(checked_mul(value, static_cast<Rep>(factor)));
        } else {
            // The quotient times the divisor gives the number back only for a
            // whole number.  The product cannot overflow, and GCC 16 builds
            // the Option of this form with no merge of its two paths, where a
            // test of the remainder costs a jump and a mask of each cell.
            constexpr Rep divisor = static_cast<Rep>(factor);
            Rep const quotient = static_cast<Rep>(value / divisor);
            if (static_cast<Rep>(quotient * divisor) != value) return none;
            return Converted::some(Quantity<ToUnit, Rep>{quotient});
        }
    }
}

}  // namespace foundation::core
