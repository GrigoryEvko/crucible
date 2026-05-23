// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-183 SchedulerPolicyLattice, mismatch class #2 of 2:
// At<P> SINGLETON GRADE DISJOINTNESS.
//
// Each lattice's `At<P>::element_type` is a DISTINCT empty struct nested
// inside that lattice's own At<> template.  Two such singletons from
// DIFFERENT lattices (SchedulerPolicyLattice vs PinningRequirementLattice)
// are unrelated types with no conversion between them — even though both
// are empty and both EBO-collapse identically inside a Graded<> carrier.
// This pins that a future V-186 `Graded<Absolute,
// SchedulerPolicyLattice::At<P>, V>` grade cannot be silently
// initialized from a foreign-lattice grade: the Agent-6 composition
// keeps the scheduler-policy, pinning, suspend, and determinism axes
// disjoint, and the V-186 federation-cache row_hash keys on the
// wrapper's grade type — so a cross-axis grade swap would corrupt the
// cache key.
//
// Distinct from neg_scheduler_policy_cross_lattice_mixing.cpp, which
// fails at a lattice OP argument; here the failure is a direct grade
// element_type assignment between two sibling lattices' singletons.
//
// Expected diagnostic: no match for / cannot convert / conversion from /
// no viable.

#include <crucible/algebra/lattices/PinningRequirementLattice.h>
#include <crucible/algebra/lattices/SchedulerPolicyLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    SchedulerPolicyLattice::At<SchedulerPolicy::Fifo>::element_type sched_grade{};

    // Should FAIL: a PinningRequirement At<> grade cannot be initialized
    // from a SchedulerPolicy At<> grade — they are unrelated empty class types.
    PinningRequirementLattice::At<PinningRequirement::PerCore>::element_type pin_grade = sched_grade;

    return static_cast<int>(
        static_cast<PinningRequirement>(pin_grade) == PinningRequirement::PerCore);
}
