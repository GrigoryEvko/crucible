// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D09 audit fixture — fifth extractor in the universal
// GradedWrapper reading surface.  graded_type_of_t<W> projects the
// substrate's bare Graded<M, L, T> instance, complementing
// value_type_of_t which projects the wrapper's user-facing
// value_type (these can differ in regime-3, e.g., AppendOnly's
// element type vs container).
//
// Like its four siblings, graded_type_of_t carries an INDEPENDENT
// `requires IsGradedWrapper<W>` constraint.  A refactor that drops
// the constraint on graded_type_of_t while leaving the other four
// extractors correctly constrained would slip through the existing
// neg-compiles but is caught here.
//
// Specifically catches the bug-class where the substrate-projecting
// extractor falls through to an opaque "no type named graded_type in
// int" diagnostic mid-alias-body, instead of producing the clean
// requires-clause rejection naming IsGradedWrapper.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/GradedExtract.h>

int main() {
    // int is not a GradedWrapper → graded_type_of_t alias is ill-formed.
    using G = crucible::safety::extract::graded_type_of_t<int>;
    G const g{};
    (void)g;
    return 0;
}
