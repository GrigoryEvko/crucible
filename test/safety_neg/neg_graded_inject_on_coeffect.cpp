// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-006: calling Graded::inject() on a Coeffect modality.
//
// `inject()` is the relative-monad unit — only defined on Graded
// instantiated at ModalityKind::RelativeMonad.  The class template
// carries `requires RelativeMonadModality<M>` on the method itself,
// so attempting it on a Graded<Coeffect, ...> produces a clean
// "constraints not satisfied" diagnostic at the call site.
//
// This test pins the contract: Coeffect modality (FIXY-G11, resource-
// consumption semiring) MUST NOT admit inject().  Coeffect grades
// compose via a semiring (Petricek-Orchard-Mycroft 2014; Brunel-
// Gaboardi-Mazza-Zdancewic 2014): sequential = +, parallel = max,
// repetition = ·.  Construction at a specific coeffect grade requires
// the caller to compute the polynomial budget — there is no unit
// operation that bottles "consumes nothing" into a Coeffect Graded
// because zero-cost is the SEMIRING ZERO and is recovered via the
// lattice's L::bottom(), not via a structural unit.  Future Graded
// refactors that loosen inject()'s gate would silently allow
// Graded<Coeffect, L, T>::inject(plain, custom_budget) — bypassing
// the semiring composition discipline.
//
// Expected diagnostic: "constraints not satisfied" / "RelativeMonadModality".

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

int main() {
    // Coeffect modality (NOT RelativeMonad).
    using G = Graded<ModalityKind::Coeffect,
                     QttSemiring::At<QttGrade::One>,
                     int>;

    // Should FAIL: inject() requires RelativeMonadModality<M>, M is Coeffect.
    auto g = G::inject(42, {});
    return g.peek();
}
