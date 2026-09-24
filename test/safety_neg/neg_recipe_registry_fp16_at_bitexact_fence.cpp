// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A value at ULP_FP16 must not reach a consumer that requires BITEXACT.
// ULP_FP16 is three tiers below BITEXACT.  Two starter recipes,
// f16_f32accum_tc and bf16_f32accum_tc, are BITEXACT_TC with FP16 or BF16
// output, and tolerance_of maps them to ULP_FP16.  A configuration error
// that selects one of them for a BITEXACT consumer must fail here.
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
    NumericalTier<Tolerance::ULP_FP16, int> fp16_value{42, {}};
    int result = bitexact_fence_consumer(std::move(fp16_value));
    return result;
}
