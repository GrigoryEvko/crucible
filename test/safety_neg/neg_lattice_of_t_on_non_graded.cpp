// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Sister fixture to neg_grade_of_t_on_non_graded.cpp: the four
// universal extractors (value_type_of_t / lattice_of_t / grade_of_t
// / modality_of_v) each carry their `requires IsGradedWrapper<W>`
// constraint independently.  A refactor that drops the constraint
// from lattice_of_t while leaving grade_of_t correctly constrained
// would slip through the grade_of_t neg-compile but is caught here.
//
// Specifically uses a "lookalike" struct that has SOME of the
// GradedWrapper surface (a nested value_type) but is missing the
// load-bearing graded_type / lattice_type / modality fields.  This
// is the exact bug-class the GradedWrapper concept's CHEAT-1 — C5
// clauses guard against — see algebra/GradedTrait.h's
// test_concept_cheat_probe harness.  The neg-compile fixture
// confirms the constraint propagates THROUGH lattice_of_t at the
// alias-declaration boundary, not just at concept-eval inside the
// substrate.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/GradedExtract.h>

namespace {
struct Lookalike {
    // Has value_type but NOTHING ELSE — fails the GradedWrapper
    // concept's graded_type / lattice_type / modality clauses.
    using value_type = int;
};
}

int main() {
    using L = crucible::safety::extract::lattice_of_t<Lookalike>;
    L const l{};
    (void)l;
    return 0;
}
