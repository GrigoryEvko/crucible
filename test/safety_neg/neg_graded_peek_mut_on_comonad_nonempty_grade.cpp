// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Graded::peek_mut() on a Comonad-modality value
// whose grade type is NON-EMPTY.
//
// The refined gate on Graded::peek_mut requires:
//
//   (AbsoluteModality<M> || std::is_empty_v<grade_type>)
//
// — mutation is admitted ONLY when the grade is orthogonal to the
// value's content (Absolute modality) OR when no runtime grade
// information exists (empty grade, e.g. ConfLattice::At<Conf::Secret>).
// For Comonad / RelativeMonad over a non-empty grade, the grade
// encodes information ABOUT the value's identity (Secret<T>'s
// classification of these specific bytes, Tagged<T, Source>'s
// provenance of this value); raw mutation would silently invalidate
// what the grade is asserting.
//
// This test pins the gate's BOTH-ARMS structure: a refactor that
// tightened the gate to AbsoluteModality alone would still be
// SOUND but unnecessarily restrictive (would break MIGRATE-3
// Secret<T>::zeroize and MIGRATE-4 Tagged<T, Source>::value_mut
// which legitimately need mutation through the empty-grade arm).
// A refactor that LOOSENED the gate to admit Comonad+non-empty would
// silently allow `Graded<Comonad, ConfLattice, T>::peek_mut()` (full
// ConfLattice — non-empty grade encoding which Conf level THIS
// value is at) — defeating the comonad's information-flow contract.
//
// We test the LOOSENING direction here: Comonad + non-empty grade
// MUST reject.  The empty-grade case is independently verified by
// MIGRATE-3 Secret<>::zeroize compiling in a positive test.
//
// Diagnostic: GCC concept-failure ("constraints not satisfied")
// pointing at the requires clause naming AbsoluteModality /
// std::is_empty_v<grade_type>.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ConfLattice.h>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

int main() {
    // Graded<Comonad, ConfLattice, int> — full ConfLattice has
    // element_type = Conf (non-empty), modality = Comonad (not
    // Absolute).  Both arms of the refined gate fail.
    using G = Graded<ModalityKind::Comonad, ConfLattice, int>;
    G g{};

    // Should FAIL: peek_mut requires
    //   (AbsoluteModality<Comonad> || std::is_empty_v<Conf>)
    // Both clauses are false; the requires clause rejects.
    int& mut_ref = g.peek_mut();
    return mut_ref;
}
