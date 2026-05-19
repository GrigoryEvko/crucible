// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-09 fixture: `protocol<Proto>` requires
// `IsSessionProtocol<Proto>` — a fundamental type (here `int`) is not
// a class and is rejected at template instantiation rather than
// silently producing an ill-typed downstream resolver projection.
//
// Pre-M-09 this compiled silently; resolver would project `int` into
// the Protocol slot of `safety::fn::Fn<...>` and surface as a wall of
// substitution errors three layers down.  Post-M-09 the rejection
// fires HERE with the named concept in the diagnostic.
//
// Expected diagnostic: constraints not satisfied / IsSessionProtocol
// / is_class / no matching function.

#include <crucible/fixy/Grant.h>

namespace gr = crucible::fixy::grant;

int main() {
    // Should FAIL: `int` is a fundamental type, not a class →
    // IsSessionProtocol<int> evaluates to false → the requires-clause
    // on `protocol<Proto>` rejects.
    [[maybe_unused]] auto bad = gr::protocol<int>{};
    return 0;
}
