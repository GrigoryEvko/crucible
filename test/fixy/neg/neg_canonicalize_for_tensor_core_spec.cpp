// The BITEXACT_TC tier keeps tensor cores, and FP16 and BF16 fragments
// give different bits on different vendors.  A content hash of a value
// under that tier is not portable, so canonicalize_for refuses the tier.
// The fixture calls the float overload, and the fixture of the ORDERED
// tier calls the double overload.

#include <fixy/fp/Canonicalize.h>

namespace fp = fixy::fp;

inline constexpr fp::CanonicalizeRecipeSpec kTensorCoreSpec{::foundation::core::RoundingMode::RN,
                                                            fp::ReductionDeterminism::BITEXACT_TC};

int main() {
    [[maybe_unused]] const auto folded = fp::canonicalize_for<kTensorCoreSpec>(1.5f);
    return 0;
}
