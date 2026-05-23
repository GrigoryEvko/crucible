// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-181 SuspendBehaviorLattice, mismatch class #2 of 2:
// At<B> SINGLETON GRADE DISJOINTNESS.
//
// Each lattice's `At<B>::element_type` is a DISTINCT empty struct nested
// inside that lattice's own At<> template.  Two such singletons from
// DIFFERENT lattices (SuspendBehaviorLattice vs DetSafeLattice) are
// unrelated types with no conversion between them — even though both are
// empty and both EBO-collapse identically inside a Graded<> carrier.
// This pins that a future V-188 `Graded<Absolute,
// SuspendBehaviorLattice::At<B>, P>` grade cannot be silently
// initialized from a foreign-lattice grade: the V-184 ClockSourceLattice
// composition keeps the suspend, determinism, and pinning axes disjoint,
// and the V-188 federation-cache row_hash keys on the wrapper's grade
// type — so a cross-axis grade swap would corrupt the cache key.
//
// Distinct from neg_suspend_behavior_cross_lattice_mixing.cpp, which
// fails at a lattice OP argument; here the failure is a direct grade
// element_type assignment between two sibling lattices' singletons.
//
// Expected diagnostic: no match for / cannot convert / conversion from /
// no viable.

#include <crucible/algebra/lattices/DetSafeLattice.h>
#include <crucible/algebra/lattices/SuspendBehaviorLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    SuspendBehaviorLattice::At<SuspendBehavior::KeepsTicking>::element_type suspend_grade{};

    // Should FAIL: a DetSafeTier At<> grade cannot be initialized from a
    // SuspendBehavior At<> grade — they are unrelated empty class types.
    DetSafeLattice::At<DetSafeTier::Pure>::element_type det_grade = suspend_grade;

    return static_cast<int>(
        static_cast<DetSafeTier>(det_grade) == DetSafeTier::Pure);
}
