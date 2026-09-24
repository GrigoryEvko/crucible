// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D09 audit fixture — sister to neg_grade_of_t_on_non_graded /
// neg_lattice_of_t_on_non_graded.  Each of the FOUR universal
// GradedWrapper extractors (value_type_of_t / lattice_of_t /
// grade_of_t / modality_of_v) carries its `requires IsGradedWrapper<W>`
// constraint INDEPENDENTLY.  A refactor that drops the constraint on
// value_type_of_t while leaving the other three correctly constrained
// would slip through the existing neg-compiles but is caught here.
//
// Specifically, value_type_of_t is the FIRST extractor in the
// dispatcher's reading order — it determines the per-element type the
// dispatcher feeds to UnaryTransform / BinaryTransform / Reduction
// callbacks.  Loosening its constraint to admit `int` (a non-wrapper)
// would let `value_type_of_t<int>` resolve to a substitution failure
// deep inside `typename std::remove_cvref_t<W>::value_type` —
// reaching the alias-body line, NOT the requires-clause boundary —
// producing an opaque "no type named value_type in int" diagnostic.
// With the constraint, the failure is a clean requires-clause
// rejection naming IsGradedWrapper.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/GradedExtract.h>

int main() {
    // int is not a GradedWrapper → value_type_of_t alias is ill-formed.
    using V = crucible::safety::extract::value_type_of_t<int>;
    V const v{};
    (void)v;
    return 0;
}
