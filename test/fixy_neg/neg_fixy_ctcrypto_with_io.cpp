// fixy_neg: CtCrypto-shape with IO trips §30.14 implicit-flow corpus.
//
// HS14 floor for FIXY-AUDIT-B3 (CtCrypto).  CtCrypto pins as_secret +
// empty Effect row to keep the constant-time discipline self-consistent
// — adding `with_io` to a CtCrypto-shape pack creates an implicit-flow
// channel that the §30.14 corpus rejects via classified-io-without-
// declassify pattern.
//
// Expected diagnostic: "NotInTheoryCorpus" — the corpus-rejection path
// surfaces through IsAccepted's theory hook.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // Hand-rolled "broken CtCrypto" — same engagements as
    // stance::CtCrypto but Effect switched from `with<>` to `with_io`.
    // The §30.14 detector fires because `as_secret` + `with<IO>` +
    // (no declassify) matches the implicit-flow pattern.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>,
        strict<D::Usage>,
        gr::with_io,           // <-- IO added; defeats CT discipline
        gr::as_secret,
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>>(42);
    (void)bad;
    return 0;
}
