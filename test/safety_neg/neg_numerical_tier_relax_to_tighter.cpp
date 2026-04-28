// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling NumericalTier<LooserTier, T>::relax<TighterTier>()
// when TighterTier > LooserTier in the ToleranceLattice.
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `ToleranceLattice::leq(LooserTier, T_at)` to a permissive form
// (e.g. `true` while debugging) — would silently let a value
// computed under ULP_FP16 discipline claim BITEXACT compliance,
// defeating the §10 precision-budget calibrator's per-op grading
// AND the cross-vendor numerics CI's per-recipe ULP gate
// (MIMIC.md §41).
//
// The lattice direction: BITEXACT is TIGHTER than ULP_FP16 (top of
// the chain).  Going DOWN (BITEXACT → ULP_FP16) is allowed — a
// stronger promise still satisfies a weaker requirement.  Going UP
// (ULP_FP16 → BITEXACT) is forbidden — the looser computation does
// NOT meet the stricter contract; no way to conjure the stronger
// promise from the weaker.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/NumericalTier.h>

using namespace crucible::safety;

int main() {
    NumericalTier<Tolerance::ULP_FP16, int> fp16_value{42};

    // Should FAIL: relax<BITEXACT> on a ULP_FP16-pinned wrapper.
    // The requires-clause `ToleranceLattice::leq(BITEXACT, ULP_FP16)`
    // is FALSE — BITEXACT is above ULP_FP16 in the chain — so the
    // relax<> overload is excluded and there's no other relax<> to
    // resolve to.
    auto bx = std::move(fp16_value).relax<Tolerance::BITEXACT>();
    return bx.peek();
}
