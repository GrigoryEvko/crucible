// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-006: calling Graded::extract() on a Coeffect modality.
//
// `extract()` is the comonad counit — only defined on Graded
// instantiated at ModalityKind::Comonad.  The class template carries
// `requires ComonadModality<M>` on the method itself, so attempting
// the call on a Graded<Coeffect, ...> produces a clean "constraints
// not satisfied" diagnostic at the call site.
//
// This test pins the contract: Coeffect modality (FIXY-G11, resource-
// consumption semiring per Petricek-Orchard-Mycroft 2014 / Brunel-
// Gaboardi-Mazza-Zdancewic 2014) MUST NOT admit extract().  The
// Coeffect grade is a polynomial in input-size encoding the resource
// budget consumed; there is no co-unit (no canonical way to project
// "the resource" out of a budget that conflates compute / memory /
// energy).  Future Graded refactors that loosen extract()'s gate
// would silently erase this discipline.
//
// Expected diagnostic: "constraints not satisfied" / "ComonadModality".

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>

#include <utility>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

int main() {
    // Coeffect modality (NOT Comonad).
    using G = Graded<ModalityKind::Coeffect,
                     QttSemiring::At<QttGrade::One>,
                     int>;
    G g{};

    // Should FAIL: extract() requires ComonadModality<M>, M is Coeffect.
    return std::move(g).extract();
}
