// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Graded::extract() on a non-Comonad modality.
//
// `extract()` is the comonad counit — only defined on Graded
// instantiated at ModalityKind::Comonad.  The class template carries
// `requires ComonadModality<M>` on the method itself, so attempting
// the call on a Graded<Absolute, ...> produces a clean "constraints
// not satisfied" diagnostic at the call site.
//
// This test pins the contract: future Graded refactors must NOT
// open extract() to non-Comonad modalities (which would silently
// erase the Secret<T>'s declassification discipline that
// Graded<Comonad, ConfLattice, T> = Secret<T> rests on).

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>

#include <utility>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

int main() {
    // Absolute modality (NOT Comonad).
    using G = Graded<ModalityKind::Absolute,
                     QttSemiring::At<QttGrade::One>,
                     int>;
    G g{};

    // Should FAIL: extract() requires ComonadModality<M>, M is Absolute.
    return std::move(g).extract();
}
