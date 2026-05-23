// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-183 SchedulerPolicyLattice, mismatch class #1 of 2:
// CROSS-LATTICE ELEMENT MIXING at a lattice op.
//
// SchedulerPolicyLattice::leq's signature requires two SchedulerPolicy
// values.  Passing a PinningRequirement value (the SIBLING Agent-6
// clock/sched axis, also uint8_t underlying) as the second argument is a
// strong-enum type-mismatch — `enum class` admits NO implicit cross-enum
// conversion.  This pins the TypeSafe disjointness the Agent-6
// composition depends on: the scheduler-policy axis and the pinning axis
// COMPOSE via wrapper-nesting, never via a single lattice op.  A
// scheduler-policy value must never be silently compared against a
// pinning-requirement value.
//
// Mirrors test/safety_neg/neg_pinning_requirement_cross_lattice_mixing.cpp.
//
// Expected diagnostic: no match for / cannot convert / no matching
// function / invalid operands.

#include <crucible/algebra/lattices/PinningRequirementLattice.h>
#include <crucible/algebra/lattices/SchedulerPolicyLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    SchedulerPolicy    sched_val = SchedulerPolicy::Fifo;
    PinningRequirement pin_val   = PinningRequirement::PerCore;

    // Should FAIL: SchedulerPolicyLattice::leq requires two
    // SchedulerPolicy values; passing a PinningRequirement as the second
    // argument is a type mismatch (no cross-enum implicit conversion).
    return static_cast<int>(
        SchedulerPolicyLattice::leq(sched_val, pin_val));
}
