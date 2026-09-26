// A divisor of 256 is not defined on a std::uint8_t.  Converted to the
// value type the divisor would be 0, and the predicate would take a
// remainder by zero.  The checked mint refuses the pair at its gate.

#include <fixy/Refined.h>

#include <cstdint>

int main() {
    auto refused = fixy::mint_refined<fixy::divisible_by<256>>(std::uint8_t{0});
    return refused.value();
}
