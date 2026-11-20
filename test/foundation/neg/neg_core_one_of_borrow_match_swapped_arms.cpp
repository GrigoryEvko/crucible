// The arms of the borrow match of a OneOf are positional.  Celsius and Kelvin
// convert into each other, so arms in the wrong order would each read the
// other alternative through a conversion: 20 degrees Celsius would read as
// 293.15.  The borrow match takes only an arm whose parameter is the
// alternative at its position, or a const reference to it, so it refuses the
// swapped arms.

#include <foundation/core/OneOf.h>

struct Celsius;

struct Kelvin {
    double value = 0.0;
    constexpr Kelvin() noexcept = default;
    constexpr explicit Kelvin(double degrees) noexcept : value{degrees} {}
    constexpr Kelvin(Celsius celsius) noexcept;
};

struct Celsius {
    double value = 0.0;
    constexpr Celsius() noexcept = default;
    constexpr explicit Celsius(double degrees) noexcept : value{degrees} {}
    constexpr Celsius(Kelvin kelvin) noexcept : value{kelvin.value - 273.15} {}
};

constexpr Kelvin::Kelvin(Celsius celsius) noexcept : value{celsius.value + 273.15} {}

int main() {
    ::foundation::core::OneOf<Celsius, Kelvin> const reading{Celsius{20.0}};
    double const seen = reading.match([](Kelvin kelvin) noexcept { return kelvin.value; },
                                      [](Celsius celsius) noexcept { return celsius.value; });
    return seen > 293.0 ? 0 : 1;
}
