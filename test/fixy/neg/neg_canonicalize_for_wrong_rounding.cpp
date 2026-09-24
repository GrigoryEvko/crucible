// Merkle-folding under a rounding mode no default kernel realizes would
// lock the content hash to that mode.  canonicalize_for demands
// round-to-nearest, ties to even.

#include <fixy/fp/Canonicalize.h>

namespace fp = fixy::fp;

inline constexpr fp::CanonicalizeRecipeSpec kTowardZeroSpec{fp::RoundingMode::RZ,
                                                            fp::ReductionDeterminism::BITEXACT_STRICT};

int main() {
    [[maybe_unused]] const auto folded = fp::canonicalize_for<kTowardZeroSpec>(1.5);
    return 0;
}
