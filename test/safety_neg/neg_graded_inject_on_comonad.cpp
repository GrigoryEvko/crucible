// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Graded::inject() on a non-RelativeMonad modality.
//
// `inject()` is the relative-monad unit — only defined on Graded
// instantiated at ModalityKind::RelativeMonad.  The class template
// carries `requires RelativeMonadModality<M>` on the method itself,
// so attempting it on a Graded<Comonad, ...> produces a clean
// "constraints not satisfied" diagnostic at the call site.
//
// This test pins the contract: future Graded refactors must NOT
// open inject() to non-RelativeMonad modalities (which would
// silently allow Secret<T>::inject(plain) — bypassing the
// classification discipline that Tagged<T, source::FromUser>'s
// retag-only construction depends on).

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ConfLattice.h>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

int main() {
    // Comonad modality (NOT RelativeMonad).
    using G = Graded<ModalityKind::Comonad,
                     ConfLattice::At<Conf::Secret>,
                     int>;

    // Should FAIL: inject() requires RelativeMonadModality<M>, M is Comonad.
    auto g = G::inject(42, {});
    return g.peek();
}
