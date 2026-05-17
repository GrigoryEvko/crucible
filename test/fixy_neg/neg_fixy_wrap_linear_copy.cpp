// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-WRAP fixture #1: Linear<T> via fixy::wrap rejects copy
// construction.
//
// Violation: Linear<T> is move-only; CSL ownership axiom.  The
// using-declaration in fixy/Wrap.h must preserve the deleted copy
// constructor identically.
//
// Expected diagnostic: substring "move-only" / "deleted function".

#include <crucible/fixy/Wrap.h>

namespace fw = crucible::fixy::wrap;

struct TypeFixyWrapLinearCopy {
    int value = 0;
};

int main() {
    fw::Linear<TypeFixyWrapLinearCopy> a =
        fw::mint_linear<TypeFixyWrapLinearCopy>(TypeFixyWrapLinearCopy{1});

    // Should FAIL: Linear<T> deletes copy with reason
    //   "Linear<T> is move-only; use std::move or drop()".
    fw::Linear<TypeFixyWrapLinearCopy> b = a;
    (void)b;
    return 0;
}
