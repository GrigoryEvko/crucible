// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// RecipeRegistry::by_name_pinned<RELAXED>(name) gives a
// NumericalTier<RELAXED, const NumericalRecipe*> for an ORDERED or an
// UNORDERED recipe.  A consumer that requires BITEXACT must refuse it.
// An ORDERED reduction can land up to 4 ULP away, and a consumer that
// accepts it at a BITEXACT site breaks the 0-ULP replay contract.
//
// The lattice runs RELAXED, ULP_INT8, ULP_FP8, ULP_FP16, ULP_FP32,
// ULP_FP64, BITEXACT from bottom to top.  satisfies_v<W, BITEXACT> is
// leq(BITEXACT, RELAXED), which is false.  This fixture is the gap of six
// tiers.  The fp64 fixture is the gap of one tier, and the fp16 fixture
// is the gap of three tiers.
//
// Expected diagnostic: the satisfies_v<W, BITEXACT> constraint of the
// consumer is not satisfied for a band at RELAXED.

#include <fixy/Bands.h>

#include <utility>

using ::fixy::NumericalTier;
using ::fixy::Tolerance;

// A consumer at the BITEXACT fence, for example the CPU oracle or the
// replay check.
template <typename W>
    requires ::fixy::satisfies_v<W, Tolerance::BITEXACT>
static int bitexact_fence_consumer(W wrapped) noexcept {
    (void)std::move(wrapped).consume();
    return 0;
}

int main() {
    NumericalTier<Tolerance::RELAXED, int> relaxed_value{42, {}};
    int result = bitexact_fence_consumer(std::move(relaxed_value));
    return result;
}
