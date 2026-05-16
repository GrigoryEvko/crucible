// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
// FIXY-A6 — a grant tag whose `relaxes` engages a different dim than
// the one the author intended.  `grant::copy` engages dim::Usage; the
// remaining 19 explicit-accepts cover every dim EXCEPT dim::Usage —
// because the author thought "grant::copy handles Usage so I need
// 19 acks for the others" but forgot that the ENGAGEMENT check is
// per-dim membership in the Grants pack, not per-grant existence.
//
// Real-world authoring trap.  Expected diag FixyNotEngaged_Usage.

#include <crucible/fixy/Reject.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

static_assert(cf::IsAccepted<
    cg::copy,                                          // engages Usage — REDUNDANT here
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    // dim::Usage NOT explicit-accepted (cg::copy DOES engage it,
    //   so this should accept — included as positive cross-check that
    //   redundancy of cg::copy + accept won't trip the rejection)
    // ... but we intentionally REMOVE Effect to force rejection:
    // dim::Effect intentionally omitted
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>, "FixyNotEngaged_Effect");

int main() { return 0; }
