// fixy_neg: mint_fn rejects an unengaged-axis Grants pack.
//
// HS14 floor for FIXY-AUDIT-D2.  mint_fn's `requires
// IsAccepted<Type, Grants...>` clause must fire constraint  // fixy-A4-023: post-H-05 IsAccepted (was IsAcceptedFn).
// satisfaction failure on `AllDimsEngaged<Grants...>` when the pack
// omits any axis.  This pins the constraint pathway distinct from
// fn<>'s class-body static_assert: the diagnostic surfaces inside
// overload resolution rather than during class-body instantiation.
//
// Expected diagnostic: "AllDimsEngaged" — the satisfaction-failure
// chain names the offending grant-level concept.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 19-axis pack omitting Usage — IsAccepted must reject via
    // AllDimsEngaged constraint failure.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, /* strict<D::Usage> omitted */
        strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>(42);
    (void)bad;
    return 0;
}
