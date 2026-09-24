// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 1 #4 (#857): IsSubstrate concept rejects non-Permissioned*
// types.
//
// Violation: a function constrained on `IsSubstrate<S>` rejects S
// types that have no substrate_traits specialization.  Plain int
// is offered; the concept fails.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at IsSubstrate.

#include <crucible/concurrent/Substrate.h>

namespace conc = crucible::concurrent;

template <conc::IsSubstrate S>
constexpr void requires_substrate(S const&) noexcept {}

int main() {
    int bad = 42;
    requires_substrate(bad);  // int is not a substrate
    return 0;
}
