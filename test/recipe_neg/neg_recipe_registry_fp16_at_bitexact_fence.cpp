// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A value at ULP_FP16 must not reach a consumer that requires BITEXACT.
// ULP_FP16 is three tiers below BITEXACT.  tolerance_of maps a BITEXACT_TC
// recipe with FP16 or BF16 output to ULP_FP16.  A configuration error that
// selects such a recipe for a BITEXACT consumer must fail here.
//
// Expected diagnostic: the satisfies_v<W, BITEXACT> constraint of the
// consumer is not satisfied for a band at ULP_FP16.

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
    auto fp16_value = ::fixy::mint_band<NumericalTier<Tolerance::ULP_FP16, int>>(42);
    int result = bitexact_fence_consumer(std::move(fp16_value));
    return result;
}
