// Negative-compile test. This file must fail to compile.
//
// Each arm of a match names enumerators of the enumeration of the value.
// The second arm names an enumerator of Order, so the match takes no such
// arm.
//
// Expected diagnostic: no match_enum takes the arms, and the constraint
// ArmOf refuses the arm of Order.

#include <foundation/core/EnumMatch.h>

#include <cstdint>

enum class Strength : std::uint8_t {
    none,
    acquire
};
enum class Order : std::uint8_t {
    none,
    acquire
};

int main() {
    namespace core = ::foundation::core;
    return core::match_enum(Strength::none, core::arm<Strength::none>([] noexcept { return 0; }),
                            core::arm<Order::acquire>([] noexcept { return 1; }));
}
