// Aligned<6>: an alignment that is not a power of two.
//
// The test of Aligned reads the low bits of an address under the mask
// N - 1, which is alignment to N only for a power of two.  Aligned<6>
// would admit the address 2, and the rule aligned_weakens would then
// prove Aligned<3>, which refuses the address 2.  So the predicate refuses
// each alignment that is not a power of two.

#include <fixy/Predicates.h>

int main() {
    [[maybe_unused]] ::fixy::Aligned<6> six_byte_alignment{};
    return 0;
}
