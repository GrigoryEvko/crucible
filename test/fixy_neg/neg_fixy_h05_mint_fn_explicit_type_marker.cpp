// fixy_neg: mint_fn rejects explicit Type-marker via wrapper-discipline.
//
// HS14 floor for fixy-H-05.  `mint_fn<Type, Grants...>(value)`'s
// requires-clause was switched from the now-deleted `IsAcceptedFn`
// alias to the public wrapper-discipline `IsAccepted` (Reject.h
// §IsAccepted).  The renamed gate auto-injects the
// `ImplicitTypeMarker` (a private `accept_default_strict_for<Type>`)
// into the underlying engagement check, so callers MUST NOT spell
// `strict<D::Type>` in the explicit Grants pack — doing so engages the
// Type axis TWICE and the constraint fails.
//
// This fixture pins that mint_fn's structural defense is intact after
// the H-05 refactor: it fires the SAME constraint-failure diagnostic
// for the duplicate-Type case as it would for any other duplicate
// engagement (e.g., two Usage grants in a single pack — the H-02
// branched tier-4 chain).
//
// Distinct from neg_fixy_mint_fn_invalid_grant.cpp (which targets
// mint_fn with a raw `int` in the pack — tier 2 of the static_assert
// chain): this fixture pins the TIER-4 path through mint_fn's
// requires-clause when the duplicate engagement is on the Type axis
// (caused by user-spelled explicit marker on top of auto-injection).
//
// Expected diagnostic: constraint failure on mint_fn's
// `requires IsAccepted<int, Grants...>` clause, naming
// `UniqueEngagementPerAxis` and the duplicate Type axis.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 20-axis pack including explicit strict<D::Type>.  mint_fn
    // auto-injects ImplicitTypeMarker via IsAccepted, so this pack
    // double-engages Type and the requires-clause fails.
    auto bad = fixy::mint_fn<int,
        strict<D::Type>,                          // ← explicit Type marker
        strict<D::Refinement>, strict<D::Usage>,
        strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>(42);
    (void)bad;
    return 0;
}
