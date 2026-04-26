// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Graded::peek_mut() on a RelativeMonad-modality
// value whose grade type is NON-EMPTY.
//
// Symmetric to neg_graded_peek_mut_on_comonad_nonempty_grade.cpp.  The
// refined gate on Graded::peek_mut requires:
//
//   (AbsoluteModality<M> || std::is_empty_v<grade_type>)
//
// — for RelativeMonad over a non-empty grade, BOTH arms fail.  The
// RelativeMonad form encodes information that depends on the value's
// identity (Tagged<T, Source>'s provenance of THIS value); raw
// mutation would silently invalidate what the grade is asserting
// about provenance.
//
// The Comonad case (neg_graded_peek_mut_on_comonad_nonempty_grade)
// pins half of the gate's arm-rejection set; this test pins the
// other half.  Without both, a refactor that loosened the gate to
// admit RelativeMonad+non-empty (while still rejecting Comonad+non-
// empty) would slip through the Comonad-only neg-compile undetected.
//
// Both tests together prove the gate uniformly rejects ANY
// non-Absolute modality over a non-empty grade — the SOUNDNESS of
// the gate's principle ("mutation is allowed when it can't violate
// the grade").
//
// Diagnostic: GCC concept-failure ("constraints not satisfied")
// pointing at the requires clause naming AbsoluteModality /
// std::is_empty_v<grade_type>.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ConfLattice.h>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

int main() {
    // Graded<RelativeMonad, ConfLattice, int> — full ConfLattice has
    // element_type = Conf (non-empty), modality = RelativeMonad (not
    // Absolute).  Both arms of the refined gate fail, symmetric to
    // the Comonad case.
    using G = Graded<ModalityKind::RelativeMonad, ConfLattice, int>;
    G g{};

    // Should FAIL: peek_mut requires
    //   (AbsoluteModality<RelativeMonad> || std::is_empty_v<Conf>)
    // Both clauses are false; the requires clause rejects.
    int& mut_ref = g.peek_mut();
    return mut_ref;
}
