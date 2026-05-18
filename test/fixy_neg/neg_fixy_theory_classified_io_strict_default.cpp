// fixy_neg: §30.14 corpus rejects strict-default Security × IO without declassify.
//
// HS14 floor for fixy-CR-01.  Theory.h's
// `classified_io_without_declassify` corpus entry detects the
// canonical implicit-flow shape AT BOTH SYNTACTIC FORMS:
//
//   1. Explicit `gr::as_secret`        — base form, covered by
//                                        neg_fixy_theory_classified_io.cpp
//   2. `strict<D::Security>` (=
//      `accept_default_strict_for<Security>`, resolves to
//      `SecLevel::Classified`)        — strict-default form, covered HERE
//
// Before fixy-CR-01 the corpus only detected form (1); a strict-default
// Security grant silently bypassed the gate despite semantic
// equivalence.  Theory.h §30.14 now ships a second `is_secret_grant`
// specialization for the strict-default form, anchored by a
// `static_assert` invariant tying the strict default to
// `SecLevel::Classified`.
//
// Reject sequence: IsAcceptedFn → IsAccepted → NotInTheoryCorpus →
// `!classified_io_without_declassify::matches<>()` evaluates false →
// IsAccepted concept fails → mint_fn signature substitution fails.
//
// Expected diagnostic: "NotInTheoryCorpus" — the satisfaction-failure
// chain names the corpus gate, same as the explicit-form fixture.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 19-axis pack engaging:
    //   Security = strict<D::Security>  (resolves to Classified — the
    //                                    strict default for Security)
    //   Effect   = with_io              (I/O effect)
    //   NO declassify<Policy> grant in the pack
    // Semantically identical to the explicit-as_secret form; the
    // strict-default-Security specialization closes the bypass.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with_io,            // Effect = IO
        strict<D::Security>,    // Security = strict default = Classified
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>(42);
    (void)bad;
    return 0;
}
