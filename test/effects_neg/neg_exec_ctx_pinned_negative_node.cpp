// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT (#852): ctx_numa::Pinned<N> non-negative N invariant.
//
// Violation: ctx_numa::Pinned<-1> would attempt to bind to a
// negative NUMA node id.  Real NUMA topologies expose only non-
// negative ids; a negative template parameter is structurally a typo
// and is caught by the static_assert(Node >= 0) inside Pinned<N>.
//
// Expected diagnostic: the static_assert inside ctx_numa::Pinned<N>
// fires when -1 is substituted.

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

int main() {
    using BadNuma = eff::ctx_numa::Pinned<-1>;
    BadNuma bad{};
    (void)bad;
    return 0;
}
