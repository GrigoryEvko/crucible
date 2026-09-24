// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating grade_of_t<T> where T is not a
// GradedWrapper-conforming type.  The alias is constrained on
// IsGradedWrapper<T>, so non-wrapper arguments are rejected at the
// requires clause rather than producing an opaque substitution-
// failure cascade three levels deep through W::graded_type::
// grade_type.
//
// Concrete bug-class this catches: a refactor that drops the
// `requires IsGradedWrapper<W>` constraint on grade_of_t (or any
// of its three siblings) would let `grade_of_t<int>` fail with
// "no type named graded_type in int" — a bare substitution error
// pointing into the middle of the alias body.  With the constraint,
// the failure is a single requires-clause diagnostic naming
// IsGradedWrapper as the unmet predicate.
//
// Specifically guards against a refactor that loosens the constraint
// to only check `requires algebra::is_graded_specialization_v<...>`
// on the substrate type, which would still admit anything
// incidentally exposing a graded_type but skip the full forwarder-
// fidelity check.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/GradedExtract.h>

int main() {
    // int is not a GradedWrapper → grade_of_t alias is ill-formed.
    using G = crucible::safety::extract::grade_of_t<int>;
    G const g{};
    (void)g;
    return 0;
}
