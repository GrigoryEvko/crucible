// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A value at ULP_FP64 must not reach a consumer that requires BITEXACT.
// ULP_FP64 is one tier below BITEXACT, so the refusal is structural along
// the whole lattice and not special to RELAXED.  A BITEXACT_TC recipe with
// FP64 storage maps to ULP_FP64, and it permits 1 ULP of difference
// between vendors, which the replay contract does not accept.
//
// Expected diagnostic: the satisfies_v<W, BITEXACT> constraint of the
// consumer is not satisfied for a band at ULP_FP64.

#include <fixy/Bands.h>

#include <utility>

using ::fixy::NumericalTier;
using ::fixy::Tolerance;

template <typename W>
    requires ::fixy::satisfies_v<W, Tolerance::BITEXACT>
static int bitexact_fence_consumer(W wrapped) noexcept {
    (void)std::move(wrapped).consume();
    return 0;
}

int main() {
    NumericalTier<Tolerance::ULP_FP64, int> fp64_value{42, {}};
    int result = bitexact_fence_consumer(std::move(fp64_value));
    return result;
}
