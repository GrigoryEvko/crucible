// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D6 fixture #2: `mint_vigil_mode_bridge` via the
// `fixy::bridge::` re-export rejects when the call site passes
// `nullptr` instead of an lvalue Vigil reference.
//
// Violation: the substrate signature takes `const Vigil&` — a
// `nullptr_t` argument cannot bind to a class-typed reference, so
// overload resolution fails at the parameter-type gate.
//
// Distinct rejection class from the non-Vigil-struct fixture: this
// exercises the `nullptr_t` → reference-type rejection rather than
// the "wrong class type" rejection, proving the gate fires across
// both kinds of mismatch.
//
// Expected diagnostic: "no matching function" / "cannot bind" /
// "invalid conversion" pointing at `mint_vigil_mode_bridge`.

#include <crucible/fixy/Bridge.h>

namespace fbridge = crucible::fixy::bridge;

int main() {
    [[maybe_unused]] auto handle = fbridge::mint_vigil_mode_bridge(nullptr);
    return 0;
}
