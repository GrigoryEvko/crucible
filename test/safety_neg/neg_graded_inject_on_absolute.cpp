// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Graded::inject() on an Absolute-modality value.
//
// `inject()` is the relative-monad unit — only defined on Graded
// instantiated at ModalityKind::RelativeMonad.  The class template
// carries `requires RelativeMonadModality<M>` on the method itself.
//
// Symmetric to neg_graded_inject_on_comonad.cpp (which pins inject's
// rejection on Comonad); together with this test we cover both
// non-RelativeMonad modality variants (Comonad AND Absolute) as
// rejected at the call site.  (The Relative variant is structurally
// equivalent to RelativeMonad for inject purposes; the gate uses
// `RelativeMonadModality` which matches RelativeMonad alone.)
//
// Without this test, a future refactor that loosened
// RelativeMonadModality<M> to also admit AbsoluteModality<M> would
// silently allow `Linear<T>::inject(plain)` — defeating the linearity
// discipline that Linear<T> = Graded<Absolute, QttSemiring::At<One>,
// T> rests on (Linear values are constructed via the wrapper's own
// move-only constructor, never injected from a plain value with no
// linearity tracking).
//
// Diagnostic: GCC concept-failure ("constraints not satisfied")
// pointing at `RelativeMonadModality<M>`.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

int main() {
    // Absolute modality (NOT RelativeMonad).
    using G = Graded<ModalityKind::Absolute,
                     QttSemiring::At<QttGrade::One>,
                     int>;

    // Should FAIL: inject() requires RelativeMonadModality<M>, M is Absolute.
    auto g = G::inject(42, {});
    return g.peek();
}
