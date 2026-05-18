// fixy_neg: §30.14 corpus rejects classified × Bg without declassify.
//
// HS14 floor for FIXY-AUDIT-G3.  Theory.h's
// `classified_bg_without_declassify` corpus entry detects the
// concurrent-information-flow shape: a binding engaging both
// `as_secret` (Security=Secret) AND `with<Effect::Bg>` (Effect=Bg)
// without interposing `declassify<Policy>` to authorize the cross-
// thread flow.
//
// Cite: Smith-Volpano 1998 / Sabelfeld-Sands 2000 / Hedin-Sabelfeld
// 2012.  The spawn itself is a scheduler-observable event; secret-
// dependent spawning leaks via thread-interleaving observability.
//
// Reject sequence: IsAcceptedFn → IsAccepted → NotInTheoryCorpus →
// `!classified_bg_without_declassify::matches<>()` evaluates false →
// IsAccepted concept fails → mint_fn signature substitution fails.
//
// Expected diagnostic: "NotInTheoryCorpus" — the satisfaction-failure
// chain names the corpus gate.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 19-axis pack engaging:
    //   Security = as_secret  (classified value)
    //   Effect   = with_bg    (Bg effect — background thread)
    //   NO declassify<Policy> grant in the pack
    // This is the Smith-Volpano concurrent-information-flow shape.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with_bg,            // Effect = Bg
        gr::as_secret,          // Security = Secret
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>(42);
    (void)bad;
    return 0;
}
