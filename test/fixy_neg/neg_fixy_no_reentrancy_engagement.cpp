// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
// FIXY-A-PLUS-6 — dim::Reentrancy unengaged → FixyNotEngaged_Reentrancy.
//
// Companion to neg_fixy_partial_19_of_20.cpp (which omits the same dim
// but documents the "near-enough" rejection narrative).  This fixture
// pins the canonical per-dim-omission shape so the dim coverage is
// uniform across all 20 dims.

#include <crucible/fixy/Reject.h>
namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;

static_assert(cf::IsAccepted<
    cf::accept_default_strict_for<cd::Type>,
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
    // dim::Reentrancy omitted
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>, "FixyNotEngaged_Reentrancy");

int main() { return 0; }
