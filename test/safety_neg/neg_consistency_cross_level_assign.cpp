// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning a Consistency<LEVEL_A, T> to a
// Consistency<LEVEL_B, T> when LEVEL_A != LEVEL_B.
//
// Different Level template arguments produce DIFFERENT class
// instantiations.  No converting assignment operator and no
// implicit conversion between them — the type system enforces
// per-level disjointness.  This is the load-bearing structural
// property keeping the call site that NEEDS STRONG from
// accidentally receiving EVENTUAL.
//
// Concrete bug-class this catches: a refactor that added a
// templated converting-assign operator on Consistency (e.g. for
// "convenience" of cross-level copy) would let a CAUSAL_PREFIX-
// committed shard silently flow into a STRONG consumer's slot,
// breaking the BatchPolicy<TP-axis> guarantee and producing
// inconsistent training updates only visible at convergence
// inspection 6 hours later.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/Consistency.h>

using namespace crucible::safety;

int main() {
    Consistency<Consistency_v::STRONG,        int> strong_value{42};
    Consistency<Consistency_v::CAUSAL_PREFIX, int> causal_value{7};

    // Should FAIL: strong_value and causal_value are DIFFERENT
    // types — different template instantiations of Consistency.
    // No converting assignment exists.
    strong_value = causal_value;
    return strong_value.peek();
}
