// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// HS14 fixture for the FIXY-AUDIT-LEGITGRANT closure (cheat-bypass).
//
// Pre-FIX: a user struct declaring `static constexpr DimAxis relaxes =
// X;` engaged IsAccepted via the loose has_axis_relaxes check, even
// without inheriting grant_base.  This let look-alike-by-field bypass
// the discipline that "engagement comes from a grant tag, not a
// look-alike field".
//
// Post-FIX: detail::is_legit_grant requires
//   std::is_class_v<T>
//   && std::derived_from<remove_cv_t<T>, grant_base>
//   && has_axis_relaxes<remove_cv_t<T>>
//
// A struct without grant_base derivation is rejected.  Authors who
// want a legitimate downstream grant extension inherit grant_base
// directly (open extension path documented in Grant.h).
//
// This fixture: declares 19 acks + a look-alike "grant" for the
// remaining dim.  Pre-FIX the pack would have IsAccepted; post-FIX
// it doesn't.  Pinned via static_assert that fires WhichDimUnengaged
// reports dim::Usage as first failing.

#include <crucible/fixy/Reject.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

// Look-alike grant — has `relaxes` field but does NOT derive from grant_base.
// Pre-FIX this would have engaged dim::Usage; post-FIX it doesn't.
struct ForgedUsageNonGrant {
    static constexpr cd::DimAxis relaxes = cd::Usage;
};

static_assert(cf::IsAccepted<
    ForgedUsageNonGrant,                                  // would-be Usage engagement (rejected)
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
