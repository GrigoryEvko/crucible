// fixy_neg: §30.14 corpus rejects strict-default Security × stale_to<N>
//            without declassify.
//
// HS14 floor for fixy-CR-01.  Theory.h's
// `staleness_secret_without_declassify` corpus entry detects the
// stale-replay information-flow shape AT BOTH SYNTACTIC FORMS:
//
//   1. Explicit `gr::as_secret`        — base form, covered by
//                                        neg_fixy_theory_staleness_secret.cpp
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
// Cite: Sabelfeld-Sands 2009 / Hunt-Sands 2008 (fixy-CR-16: Andrysco-
// 2015 was a misattributed paper about FP timing channels and has been
// replaced).  A classified value reachable through a non-Fresh
// staleness window without a freshness-discharging policy is a failed
// erasure — independent of whether the Security grant is spelled
// `as_secret` or `strict<Security>`.
//
// Reject sequence: IsAcceptedFn → IsAccepted → NotInTheoryCorpus →
// `!staleness_secret_without_declassify::matches<>()` evaluates false →
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
    // 20-axis pack engaging:
    //   Security  = strict<D::Security>  (Classified — strict default)
    //   Staleness = stale_to<100>        (replay window of 100 units)
    //   NO declassify<Policy> grant in the pack
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
        strict<D::Security>,                    // strict default = Classified
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Synchronization>,
        gr::stale_to<100>>(42);                 // Staleness ≠ Fresh
    (void)bad;
    return 0;
}
