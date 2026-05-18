// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-012 (#1619): Capability::consume() is rvalue-ref-qualified
// so the discipline call-site MUST be `std::move(c).consume()`.  A
// bare `c.consume()` on an lvalue must be rejected at the type level.
//
// Violation: calling consume() on an lvalue.
//
// Expected diagnostic: "cannot bind rvalue reference of type
// 'Capability<...>&&' to lvalue" / "cannot convert 'Capability<...>'
// lvalue to 'Capability<...>&&'" / "no matching member function" /
// similar.  GCC 16 typically prints "passing 'Capability<...>' as
// 'this' argument discards qualifiers" or "ref-qualified".

#include <crucible/effects/Capability.h>

namespace eff = crucible::effects;

int main() {
    auto bg = eff::testing::bg();
    auto cap = eff::mint_cap<eff::Effect::Alloc>(bg);
    cap.consume();  // lvalue: rejected by the `&&` qualifier
    (void)cap;
    return 0;
}
