// No operation takes a float quantity and an integer quantity together: add
// of bytes in a double and bytes in an integer does not compile.  The door
// from the integer to the float is round_to_float.

#include <foundation/core/Quantity.h>

#include <cstdint>

int main() {
    namespace core = ::foundation::core;
    core::Quantity<core::unit::Byte, double> const measured{2.0};
    core::Quantity<core::unit::Byte, std::uint64_t> const counted{3};
    return core::add(measured, counted).raw() > 1.0 ? 0 : 1;
}
