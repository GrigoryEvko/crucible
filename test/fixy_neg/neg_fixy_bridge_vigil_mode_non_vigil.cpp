// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D6 fixture #1: `mint_vigil_mode_bridge` via the
// `fixy::bridge::` re-export rejects when the parameter is an
// unrelated struct rather than a `const Vigil&`.
//
// Violation: callers must pass a `Vigil` instance.  An arbitrary
// user-defined struct has no `mode_session()` method, no implicit
// conversion to `Vigil`, and is therefore rejected by overload
// resolution at the parameter-type gate.
//
// Expected diagnostic: "no matching function" / "cannot bind" /
// "could not convert" pointing at `mint_vigil_mode_bridge`.

#include <crucible/fixy/Bridge.h>

namespace fbridge = crucible::fixy::bridge;

// Plausible mistake shape: a user-defined "vigil-like" type that
// looks structurally similar but is NOT crucible::Vigil.
struct FakeVigil {
    int mode = 0;
};

int main() {
    FakeVigil fake{};
    [[maybe_unused]] auto handle = fbridge::mint_vigil_mode_bridge(fake);
    return 0;
}
