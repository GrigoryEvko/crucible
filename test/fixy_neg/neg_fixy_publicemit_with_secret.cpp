// fixy_neg: PublicEmit-shape repurposed with as_secret trips §30.14.
//
// HS14 floor for FIXY-AUDIT-B3 (PublicEmit).  PublicEmit's discipline
// pins Security via `declassify<Policy>` to the Public level so the
// audit trail discharges the flow.  Swapping declassify for `as_secret`
// (still keeping the IO grant) recreates the implicit-flow pattern
// classified-io-without-declassify — §30.14 corpus fires.
//
// Expected diagnostic: "NotInTheoryCorpus".

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // Hand-rolled "broken PublicEmit" — same engagements as
    // stance::PublicEmit but with as_secret in lieu of declassify.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>,
        strict<D::Usage>,
        gr::with_io,
        gr::as_secret,         // <-- swapped from declassify; now Secret+IO
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>(42);
    (void)bad;
    return 0;
}
