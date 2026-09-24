// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Consistency<WeakerLevel, T>::relax<StrongerLevel>()
// when StrongerLevel > WeakerLevel in the ConsistencyLattice.
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `ConsistencyLattice::leq(WeakerLevel, Level)` to a permissive
// form (e.g. `true` while debugging) — would silently let a value
// committed under EVENTUAL consistency claim STRONG compliance,
// defeating the §5 BatchPolicy<Axis, Level> per-axis enforcement
// AND the Forge Phase K row-validation gate.
//
// The lattice direction: STRONG is HIGHER than CAUSAL_PREFIX (top
// of the chain).  Going DOWN (STRONG → CAUSAL_PREFIX → EVENTUAL)
// is allowed — a stronger guarantee still satisfies a weaker
// requirement.  Going UP (EVENTUAL → STRONG) is forbidden — the
// weakly-consistent value does NOT meet the stricter contract;
// no way to conjure the stronger guarantee from the weaker.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/Consistency.h>

using namespace crucible::safety;

int main() {
    Consistency<Consistency_v::EVENTUAL, int> eventual_value{42};

    // Should FAIL: relax<STRONG> on an EVENTUAL-pinned wrapper.
    // The requires-clause `ConsistencyLattice::leq(STRONG, EVENTUAL)`
    // is FALSE — STRONG is above EVENTUAL — so the relax<> overload
    // is excluded.
    auto strong = std::move(eventual_value).relax<Consistency_v::STRONG>();
    return strong.peek();
}
