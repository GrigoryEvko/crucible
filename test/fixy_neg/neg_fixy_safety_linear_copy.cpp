// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Safety fixture #1: Linear<T> via fixy:: alias rejects
// copy construction.
//
// Violation: Linear<T> is move-only at the L0 ownership axiom.
// Routing through `fixy::safety::Linear` must preserve the
// `= delete("move-only")` constraint identically.
//
// Expected diagnostic: substring "move-only" / "deleted function".

#include <crucible/fixy/Safety.h>

namespace fsaf = crucible::fixy::safety;

int main() {
    fsaf::Linear<int> a = fsaf::mint_linear<int>(42);

    // Should FAIL: copy constructor deleted with reason
    //   "Linear<T> is move-only; use std::move or drop()".
    fsaf::Linear<int> b = a;
    (void)b;
    return 0;
}
