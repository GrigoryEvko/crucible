// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
// FIXY-A-PLUS-6 — dim::Type unengaged → FixyNotEngaged_Type.
//
// dim::Type is enumerator 0 in dim::DimAxis, so when ANY dim is unengaged
// alongside Type, the first-failing-dim heuristic in WhichDimUnengaged
// returns Type.  This fixture exercises the exact case where ONLY
// dim::Type is omitted (the other 19 are explicit-accepted) — pinning
// that the conjunction reads the FIRST clause first.

#include <crucible/fixy/Reject.h>
namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;

static_assert(cf::IsAccepted<
    // dim::Type omitted
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
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
>, "FixyNotEngaged_Type");

int main() { return 0; }
