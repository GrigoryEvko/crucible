// fixy_neg: CtCrypto-shape with Security axis unengaged.
//
// HS14 floor for FIXY-AUDIT-B3 (CtCrypto).  CtCrypto engages Security
// via `as_secret`; removing that engagement without any replacement
// leaves Security unengaged.  Reject.h's per-axis engagement check
// fires `FixyNotEngaged_Security`.
//
// Expected diagnostic: "FixyNotEngaged_Security".

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // Hand-rolled "broken CtCrypto" — same engagements as
    // stance::CtCrypto MINUS the Security engagement.  IsAccepted
    // rejects via the engagement gate.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>,
        strict<D::Usage>,
        gr::with<>,
        /* gr::as_secret removed — Security unengaged */
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>>(42);
    (void)bad;
    return 0;
}
