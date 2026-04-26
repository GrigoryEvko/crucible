// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Graded::extract() on a RelativeMonad-modality value.
//
// `extract()` is the comonad counit — only defined on Graded
// instantiated at ModalityKind::Comonad.  The class template carries
// `requires ComonadModality<M>` on the method itself.
//
// Symmetric to neg_graded_extract_on_absolute.cpp (which pins extract's
// rejection on Absolute); together with this test we cover both
// non-Comonad modality variants (Absolute AND RelativeMonad) as
// rejected at the call site.
//
// Without this test, a future refactor that loosened ComonadModality<M>
// to also admit RelativeMonadModality<M> would silently allow
// `Tagged<T, Source>::extract()` — bypassing the named declassify-
// counit / counit-policy discipline that Tagged's RelativeMonad-form
// inject pairs with.  The Comonad / RelativeMonad split is the whole
// reason the substrate ships TWO unit/counit operations rather than
// one — collapsing the discipline silently would defeat the type-
// theoretic separation.
//
// Diagnostic: GCC concept-failure ("constraints not satisfied")
// pointing at `ComonadModality<M>`.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/TrustLattice.h>

#include <utility>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

namespace {
struct FromUserSource {};
}  // namespace

int main() {
    // RelativeMonad modality (NOT Comonad).
    using G = Graded<ModalityKind::RelativeMonad,
                     TrustLattice<FromUserSource>,
                     int>;
    G g{};

    // Should FAIL: extract() requires ComonadModality<M>, M is RelativeMonad.
    return std::move(g).extract();
}
