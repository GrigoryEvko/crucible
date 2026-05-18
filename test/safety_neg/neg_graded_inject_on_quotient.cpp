// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-006: calling Graded::inject() on a Quotient modality.
//
// `inject()` is the relative-monad unit — only defined on Graded
// instantiated at ModalityKind::RelativeMonad.  The class template
// carries `requires RelativeMonadModality<M>` on the method itself,
// so attempting it on a Graded<Quotient, ...> produces a clean
// "constraints not satisfied" diagnostic at the call site.
//
// This test pins the contract: Quotient modality (FIXY-G10,
// equivalence-class membership) MUST NOT admit inject().  Construction
// at a Quotient grade requires choosing a representative of the
// equivalence class — that is the user's responsibility via the
// generic two-arg ctor, NOT a structural unit operation.  Future
// Graded refactors that loosen inject()'s gate would silently
// allow Graded<Quotient, L, T>::inject(plain, class_rep) — bypassing
// the equivalence-class membership discipline.
//
// Expected diagnostic: "constraints not satisfied" / "RelativeMonadModality".

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

int main() {
    // Quotient modality (NOT RelativeMonad).
    using G = Graded<ModalityKind::Quotient,
                     QttSemiring::At<QttGrade::One>,
                     int>;

    // Should FAIL: inject() requires RelativeMonadModality<M>, M is Quotient.
    auto g = G::inject(42, {});
    return g.peek();
}
