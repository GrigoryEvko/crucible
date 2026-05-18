// fixy_neg: IsAcceptedGrants rejects duplicate per-axis engagement.
//
// HS14 floor for FIXY-AUDIT-A3.  A pack that engages the same axis
// twice (here: two `strict<D::Usage>` markers) is silently lossy
// under the resolver's "first matching grant wins" rule and signals
// author confusion.  `UniqueEngagementPerAxis` must fire at
// IsAcceptedGrants via mint_fn's requires clause.
//
// Expected diagnostic: "UniqueEngagementPerAxis" — the
// satisfaction-failure chain names the multiplicity concept.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 20-element pack: 19 distinct axes covered + a second
    // strict<D::Usage> duplicating the Usage axis.  Type is
    // implicit (mint_fn injects ImplicitTypeMarker), so every
    // axis is engaged; the duplication on Usage is the load-
    // bearing rejection cause.  IsAcceptedFn → IsAcceptedGrants →
    // UniqueEngagementPerAxis must fire.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>, strict<D::Usage>,
        strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>(42);
    (void)bad;
    return 0;
}
