// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D09 audit fixture — sister to neg_grade_of_t / neg_lattice_of_t
// / neg_value_type_of_t.  modality_of_v is the only extractor in the
// FOUND-D09 quartet that resolves to a NON-TYPE value (a constexpr
// ModalityKind enum value), making its `requires IsGradedWrapper<W>`
// guard structurally distinct from the three type-alias siblings.
//
// A refactor that drops the constraint on the variable template would
// let `modality_of_v<int>` resolve to a substitution failure deep
// inside `std::remove_cvref_t<W>::modality` — producing "no member
// modality in int" rather than the clean requires-clause rejection
// naming IsGradedWrapper.
//
// The fixture forces the non-type extractor's constraint into a
// constant-expression context (initialization of a `constexpr auto`)
// which is the canonical surface for catching variable-template
// constraint regressions.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/GradedExtract.h>

int main() {
    // int is not a GradedWrapper → modality_of_v constraint fails.
    constexpr auto m = crucible::safety::extract::modality_of_v<int>;
    (void)m;
    return 0;
}
