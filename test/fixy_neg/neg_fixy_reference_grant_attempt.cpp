// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-CVR — pre-audit, passing a REFERENCE type (e.g.,
// `grant::copy&`) as a Grants pack member produced a HARD substitution
// error inside Reject.h ("'relaxes' is not a member of 'grant::copy&'")
// rather than a clean per-dim diagnostic.  Authors who accidentally
// passed references (e.g., through a forwarding-template chain or a
// CRTP base whose primary parameter is `T&`) got an opaque error.
//
// Post-audit, `EngagedFor`'s fold pre-screens each pack member with
// the `detail::has_relaxes_member` concept; pack members without a
// `relaxes` member (including reference and pointer types) simply
// don't engage any dim.  The result is the standard reject path with
// the canonical "FixyNotEngaged_Type" first-failing diagnostic.
//
// This fixture pins the new clean-failure behavior: a pack containing
// `grant::copy&` (which does NOT engage Usage as a reference type)
// plus 19 acks for the other dims rejects on dim::Usage being
// unengaged — NOT on a substitution failure deep in <Reject.h>.

#include <crucible/fixy/Reject.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

static_assert(cf::IsAccepted<
    cg::copy&,                                          // reference — does NOT engage Usage
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
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
>, "FixyNotEngaged_Usage");

int main() { return 0; }
