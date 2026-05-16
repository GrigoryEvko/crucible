// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
// FIXY-A6 — dim::Security unengaged → expected diag FixyNotEngaged_Security.
//
// Especially load-bearing: a function that forgets to acknowledge
// classification could silently leak Classified data downstream.
// fixy refuses to compile until the author either declassifies
// explicitly (grant::declassify<Policy>) or accepts the strict
// Classified default.

#include <crucible/fixy/Reject.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;

static_assert(cf::IsAccepted<
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    // dim::Security intentionally omitted
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
>, "FixyNotEngaged_Security");

int main() { return 0; }
