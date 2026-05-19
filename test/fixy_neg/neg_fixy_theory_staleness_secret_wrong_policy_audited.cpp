// fixy_neg: §30.14 corpus rejects declassify<AuditedLogging> × stale_to
//            because AuditedLogging is the IO-axis audit-trail policy,
//            NOT the Staleness-replay discharge.
//
// fixy-A4-015 negative fixture #1.  Pre-A4-015 the
// `staleness_secret_without_declassify` matcher treated ANY
// `declassify<Policy>` grant as a universal silencer — a footgun per
// Hunt-Sands 2008 'Just Forget It' (POPL) / Sabelfeld-Myers 2003
// 'Language-based information-flow security' (IEEE J. Sel. Areas)
// which require the discharge to MATCH the axis it authorizes.  A
// policy like `AuditedLogging` is the audit-trail discharge for IO-
// export channels and says NOTHING about stale-replay / freshness.
// Using it to silence a `declassify + stale_to<N>` combination is the
// "wrong policy authorizes the wrong axis" footgun A4-015 closes.
//
// Bypass shape note: the pack engages Security via `declassify<P>`
// ALONE — `which_dim_v<declassify<P>> == DimensionAxis::Security`
// (Grant.h:301).  Combining `as_secret` would double-engage Security
// and trip `UniqueEngagementPerAxis` upstream BEFORE reaching the
// corpus.  Post-A4-015 the extended `is_secret_grant<declassify<P>>`
// specialization (Theory.h §30.14) means the matcher sees Security
// engagement via declassify alone, and the per-axis
// `axes_discharged_of<Policy>` discipline correctly rejects when the
// policy's mask omits `DischargeAxis::Staleness`.
//
// Reject sequence: IsAccepted → IsAcceptedDirect → NotInTheoryCorpus →
// `staleness_secret_without_declassify::matches<...>()` evaluates
// true (has_secret && has_stale && !has_staleness_discharge) →
// IsAccepted concept fails → mint_fn signature substitution fails.
//
// Expected diagnostic: "NotInTheoryCorpus" OR the matched corpus
// entry's struct name "staleness_secret_without_declassify" — the
// fixy-H-16 corpus_full_diagnostic_v surface names BOTH the gate
// concept and the matched entry.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
namespace sp   = crucible::safety::secret_policy;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 20-axis pack engaging:
    //   Security    = declassify<AuditedLogging>  (engages Security via which_dim_v;
    //                                              WRONG axis — IO, not Staleness)
    //   Staleness   = stale_to<100>               (replay window)
    //   Effect      = strict<Effect>              (default-strict; no IO engagement)
    // AuditedLogging's `axes_discharged_of_v == DischargeAxis::None`
    // (Hunt-Sands safe-default), so the staleness-replay axis is NOT
    // discharged → matcher fires → corpus rejects.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>,
        strict<D::Effect>,
        gr::declassify<sp::AuditedLogging>,            // Security via WRONG axis (IO)
        gr::stale_to<100>,                             // Staleness ≠ Fresh
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Synchronization>, strict<D::Regime>>(42);
    (void)bad;
    return 0;
}
