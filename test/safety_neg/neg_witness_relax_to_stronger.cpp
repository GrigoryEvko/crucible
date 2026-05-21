// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Witness<WeakerTier, T>::relax<StrongerTier>()
// when StrongerTier > WeakerTier in the WitnessLattice.
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding Witness::relax<>() — specifically, a slip
// from `WitnessLattice::leq(WeakerTier, Tier)` to a permissive form
// (e.g. `true` while debugging) — would silently let a TYPE_CHECKED
// value claim FORMALLY_VERIFIED status, defeating the V-053..V-056
// proof-strength contract.  A downstream consumer asking for
// FORMALLY_VERIFIED-floor evidence would accept a value the producer
// only type-checked, breaking the witness contract at the API
// boundary instead of in a future audit.
//
// The lattice direction: FORMALLY_VERIFIED is at the TOP of the
// witness chain.  Going DOWN (FORMALLY_VERIFIED → TEST_PASSED →
// TYPE_CHECKED → UNWITNESSED) is allowed — a stronger guarantee
// still satisfies a weaker requirement.  Going UP (TYPE_CHECKED →
// FORMALLY_VERIFIED) is forbidden — the type-checked value does NOT
// meet the stricter proof; no way to conjure the stronger evidence
// from the weaker.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on Witness::relax<>().

#include <crucible/safety/Witness.h>

using namespace crucible::safety;

int main() {
    Witness<Witness_v::TYPE_CHECKED, int> type_checked{42};

    // Should FAIL: relax<FORMALLY_VERIFIED> on a TYPE_CHECKED-pinned
    // wrapper.  The requires-clause
    // `WitnessLattice::leq(FORMALLY_VERIFIED, TYPE_CHECKED)` is
    // FALSE — FORMALLY_VERIFIED is above TYPE_CHECKED — so the
    // relax<> overload is excluded.
    auto formally = std::move(type_checked).relax<Witness_v::FORMALLY_VERIFIED>();
    return formally.peek();
}
