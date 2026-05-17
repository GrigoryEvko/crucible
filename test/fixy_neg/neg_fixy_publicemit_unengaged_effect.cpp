// fixy_neg: PublicEmit-shape with Effect axis unengaged.
//
// HS14 floor for FIXY-AUDIT-B3 (PublicEmit).  PublicEmit engages Effect
// via `with_io`; removing the IO engagement without replacement leaves
// Effect unengaged.  Reject.h's per-axis engagement check fires
// `FixyNotEngaged_Effect`.
//
// Expected diagnostic: "FixyNotEngaged_Effect".

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

namespace pe_neg_policies {
struct EmitPolicy {};
}  // namespace pe_neg_policies

int main() {
    // Hand-rolled "broken PublicEmit" — same engagements as
    // stance::PublicEmit MINUS the Effect engagement.  IsAccepted
    // rejects via the engagement gate.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>,
        strict<D::Usage>,
        /* gr::with_io removed — Effect unengaged */
        gr::declassify<pe_neg_policies::EmitPolicy>,
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>>(42);
    (void)bad;
    return 0;
}
