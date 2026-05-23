// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-182 PinningRequirementLattice, mismatch class #2 of 2:
// At<P> SINGLETON GRADE DISJOINTNESS.
//
// Each lattice's `At<P>::element_type` is a DISTINCT empty struct nested
// inside that lattice's own At<> template.  Two such singletons from
// DIFFERENT lattices (PinningRequirementLattice vs SuspendBehaviorLattice)
// are unrelated types with no conversion between them — even though both
// are empty and both EBO-collapse identically inside a Graded<> carrier.
// This pins that a future V-187 `Graded<Absolute,
// PinningRequirementLattice::At<P>, V>` grade cannot be silently
// initialized from a foreign-lattice grade: the V-184 ClockSourceLattice
// composition keeps the pinning, suspend, and determinism axes disjoint,
// and the V-187 federation-cache row_hash keys on the wrapper's grade
// type — so a cross-axis grade swap would corrupt the cache key.
//
// Distinct from neg_pinning_requirement_cross_lattice_mixing.cpp, which
// fails at a lattice OP argument; here the failure is a direct grade
// element_type assignment between two sibling lattices' singletons.
//
// Expected diagnostic: no match for / cannot convert / conversion from /
// no viable.

#include <crucible/algebra/lattices/PinningRequirementLattice.h>
#include <crucible/algebra/lattices/SuspendBehaviorLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    PinningRequirementLattice::At<PinningRequirement::PerCore>::element_type pin_grade{};

    // Should FAIL: a SuspendBehavior At<> grade cannot be initialized from
    // a PinningRequirement At<> grade — they are unrelated empty class types.
    SuspendBehaviorLattice::At<SuspendBehavior::KeepsTicking>::element_type suspend_grade = pin_grade;

    return static_cast<int>(
        static_cast<SuspendBehavior>(suspend_grade) == SuspendBehavior::KeepsTicking);
}
