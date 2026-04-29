// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Progress<WeakerClass, T>::relax<StrongerClass>()
// when StrongerClass > WeakerClass in the ProgressLattice.
//
// THE LOAD-BEARING REJECTION FOR FORGE PHASE WALL-CLOCK BUDGETS.
// Without it, an Inferlet user value (declared MayDiverge, the
// escape-hatch tier) could be re-typed as Bounded and silently
// flow into a Forge phase admission gate, defeating FORGE.md §5
// hard wall-clock budgets (Phase A = 50ms, Phase H = 500ms, etc.).
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `ProgressLattice::leq(WeakerClass, Class)` to a permissive form
// — would silently allow a MayDiverge-tier value to claim Bounded
// compliance.  The dispatcher's Forge-phase admission gate would
// then admit user-supplied unbounded code into a deadline-sensitive
// compilation phase.
//
// Lattice direction: Bounded is at the TOP (strongest termination
// guarantee — hard wall-clock bound); MayDiverge is at the BOTTOM
// (escape hatch — no termination guarantee).  Going DOWN (Bounded
// → Productive → Terminating → MayDiverge) is allowed.  Going UP
// is FORBIDDEN.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/Progress.h>

using namespace crucible::safety;

int main() {
    // Pinned at MayDiverge — escape-hatch value (e.g., Inferlet user
    // code with unbounded recursion).  This is what Forge phases
    // MUST reject; the relax<> below is the bug-introduction path.
    Progress<ProgressClass_v::MayDiverge, int> diverge_value{42};

    // Should FAIL: relax<Bounded> on a MayDiverge-pinned wrapper.
    // The requires-clause `ProgressLattice::leq(Bounded, MayDiverge)`
    // is FALSE — Bounded is above MayDiverge in the chain — so the
    // relax<> overload is excluded.  Without this fence, Inferlet
    // user code could claim Bounded compliance and silently enter
    // a Forge phase, breaking the wall-clock discipline.
    auto bounded_claim = std::move(diverge_value).relax<ProgressClass_v::Bounded>();
    return bounded_claim.peek();
}
