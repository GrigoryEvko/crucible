// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-006: calling Graded::extract() on a Quotient modality.
//
// `extract()` is the comonad counit — only defined on Graded
// instantiated at ModalityKind::Comonad.  The class template carries
// `requires ComonadModality<M>` on the method itself, so attempting
// the call on a Graded<Quotient, ...> produces a clean "constraints
// not satisfied" diagnostic at the call site.
//
// This test pins the contract: Quotient modality (FIXY-G10,
// equivalence-class membership) MUST NOT admit extract().  The
// Quotient grade names an equivalence-class representative — there
// is no co-unit (no canonical way to project "the value" out of a
// quotient when multiple values share the class).  Future Graded
// refactors that loosen extract()'s gate would silently erase this
// discipline.
//
// Expected diagnostic: "constraints not satisfied" / "ComonadModality".

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>

#include <utility>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

int main() {
    // Quotient modality (NOT Comonad).
    using G = Graded<ModalityKind::Quotient,
                     QttSemiring::At<QttGrade::One>,
                     int>;
    G g{};

    // Should FAIL: extract() requires ComonadModality<M>, M is Quotient.
    return std::move(g).extract();
}
