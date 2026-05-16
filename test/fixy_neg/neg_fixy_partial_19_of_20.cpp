// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
// FIXY-A6 — 19 of 20 dims engaged, only dim::Reentrancy missing.
// Demonstrates the conjunction has no "near-enough" relaxation —
// 19-of-20 is not 20-of-20.  Expected diag FixyNotEngaged_Reentrancy.

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
    // dim::Reentrancy intentionally omitted
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>, "FixyNotEngaged_Reentrancy");

int main() { return 0; }
