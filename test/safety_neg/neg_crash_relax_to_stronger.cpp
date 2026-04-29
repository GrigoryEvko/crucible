// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Crash<WeakerClass, T>::relax<StrongerClass>()
// when StrongerClass > WeakerClass in the CrashLattice.
//
// THE LOAD-BEARING REJECTION FOR THE OneShotFlag-guarded boundary
// DISCIPLINE (28_04_2026_effects.md §4.3.10 +
// bridges/CrashTransport.h).  Without it, an Abort-classified value
// (from a function that may have called crucible_abort) could be
// silently re-typed as NoThrow and admitted into the OneShotFlag-
// skipping fast path — defeating the recovery-aware boundary that
// keeper init/shutdown paths depend on.
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `CrashLattice::leq(WeakerClass, Class)` to a permissive form —
// would silently allow an Abort-tier value to claim NoThrow
// compatibility.  The dispatcher's NoThrow-only admission gate
// would then admit possibly-aborted values into recovery-free code.
//
// Lattice direction: NoThrow is at the TOP (strongest claim — no
// failure mode); Abort is at the BOTTOM (weakest claim — function
// may kill the process).  Going DOWN (NoThrow → ErrorReturn →
// Throw → Abort) is allowed.  Going UP is FORBIDDEN.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/Crash.h>

using namespace crucible::safety;

int main() {
    // Pinned at Abort — bytes derive from a Keeper init path that
    // may have called crucible_abort().  This is what the OneShot-
    // Flag-skipping fast path MUST reject; the relax<> below is
    // the bug-introduction path the wrapper fences.
    Crash<CrashClass_v::Abort, int> abort_value{42};

    // Should FAIL: relax<NoThrow> on an Abort-pinned wrapper.  The
    // requires-clause `CrashLattice::leq(NoThrow, Abort)` is FALSE
    // — NoThrow is above Abort in the chain — so the relax<>
    // overload is excluded.  Without this fence, abort-classified
    // values could claim NoThrow compatibility and silently bypass
    // OneShotFlag recovery checks.
    auto nothrow_claim = std::move(abort_value).relax<CrashClass_v::NoThrow>();
    return nothrow_claim.peek();
}
