// fixy_neg: §30.14 corpus rejects declassify<WireSerialize> × stale_to
//            because WireSerialize is for IO wire-format export, NOT
//            the Staleness-replay discharge.
//
// fixy-A4-015 negative fixture #2 (HS14 ≥2 floor: distinct mismatch
// class).  Variant of `..._wrong_policy_audited.cpp` proving the
// per-axis discipline holds across the WHOLE family of pre-A4-015
// declassify policies whose `axes_discharged_of_v` defaults to
// `DischargeAxis::None`: WireSerialize, HashForCompare, LengthOnly,
// UserDisplay all share the same fate when combined with stale_to —
// they don't discharge Staleness, so the matcher fires.
//
// Pre-A4-015 the matcher silenced on ANY declassify regardless of the
// policy's actual axis authority — the canonical Hunt-Sands axis-
// mismatch footgun.  Post-A4-015 the matcher consults the policy's
// `axes_discharged_of` mask and only accepts policies that
// explicitly carry `DischargeAxis::Staleness` (currently only
// `AuthorizedReplay`).
//
// Bypass shape note: Security is engaged via `declassify<P>` ALONE —
// `which_dim_v<declassify<P>> == DimensionAxis::Security`
// (Grant.h:301).  Combining `as_secret` would double-engage Security
// and trip `UniqueEngagementPerAxis` upstream BEFORE reaching the
// corpus.
//
// Reject sequence: IsAccepted → IsAcceptedDirect → NotInTheoryCorpus →
// `staleness_secret_without_declassify::matches<...>()` evaluates
// true (has_secret && has_stale && !has_staleness_discharge) →
// IsAccepted concept fails → mint_fn signature substitution fails.
//
// Expected diagnostic: "NotInTheoryCorpus" OR the matched corpus
// entry's struct name "staleness_secret_without_declassify".

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
namespace sp   = crucible::safety::secret_policy;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 20-axis pack engaging:
    //   Security    = declassify<WireSerialize>   (engages Security via which_dim_v;
    //                                              WRONG axis — IO wire-export)
    //   Staleness   = stale_to<500>               (replay window)
    //   Effect      = strict<Effect>              (default-strict; no IO engagement)
    // Per A4-015, WireSerialize's `axes_discharged_of_v ==
    // DischargeAxis::None`, so the staleness-replay axis is NOT
    // discharged → matcher fires → corpus rejects.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>,
        strict<D::Effect>,
        gr::declassify<sp::WireSerialize>,             // Security via WRONG axis (IO)
        gr::stale_to<500>,                             // Staleness ≠ Fresh
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Synchronization>, strict<D::Regime>,
        strict<D::FpMode>>(42);
    (void)bad;
    return 0;
}
