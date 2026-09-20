// Folding a double into a content hash under a tier that permits
// reordering would lock the hash to a bit pattern no other replica
// reproduces.  canonicalize_for demands a bit-exact tier.

#include <fixy/fp/Canonicalize.h>

namespace fp = fixy::fp;

inline constexpr fp::CanonicalizeRecipeSpec kOrderedSpec{fp::RoundingMode::RN, fp::ReductionDeterminism::ORDERED};

int main() {
    [[maybe_unused]] const auto folded = fp::canonicalize_for<kOrderedSpec>(1.5);
    return 0;
}
