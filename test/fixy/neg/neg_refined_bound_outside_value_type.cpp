// A floor of 256 is not defined on a std::uint8_t, which cannot hold it.
// Converted to the value type the floor would be 0, and 0 would pass.
// The checked mint refuses the pair at its gate, and the note names the
// bound that does not fit.

#include <fixy/Refined.h>

#include <cstdint>

int main() {
    auto refused = fixy::mint_refined<fixy::bounded_below<256>>(std::uint8_t{0});
    return refused.value();
}
