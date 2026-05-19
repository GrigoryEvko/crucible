// fixy_neg: §30.14 corpus rejects internal × Bg without declassify.
//
// fixy-A4-008: Theory.h's `internal_bg_without_declassify` corpus
// entry detects the concurrent-execution counterpart of fixy-H-18's
// `internal_io_without_declassify`.  Smith-Volpano 1998 + Sabelfeld-
// Myers 2003 establish that sequential IFC type systems are UNSOUND
// under concurrency: spawning a background task whose existence and
// timing observably depend on an Internal-tier value declassifies
// that tier through the scheduler's exit / progress / liveness
// surface even without any explicit I/O.  An `as_internal` binding
// (SecLevel::Internal = 2, below strict default Classified = 3) that
// engages a Bg effect without a declassify policy is the
// concurrent-channel dual of the Bell-LaPadula no-write-down
// violation.
//
// Reject sequence: IsAccepted → IsAcceptedDirect → NotInTheoryCorpus →  // fixy-A4-023: post-H-05 chain.
// `!internal_bg_without_declassify::matches<>()` evaluates false →
// IsAccepted concept fails → mint_fn signature substitution fails.
//
// Expected diagnostic: "NotInTheoryCorpus" OR the matched corpus
// entry's struct name "internal_bg_without_declassify" — the
// fixy-H-16 corpus_full_diagnostic_v surface names BOTH the gate
// concept and the matched entry.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 19-axis pack engaging:
    //   Security = as_internal  (org-internal tier, NOT classified/secret)
    //   Effect   = with_bg      (background-thread effect)
    //   NO declassify<Policy> grant in the pack
    // This is the Internal-tier concurrent no-write-down shape that
    // fixy-A4-008 added to §30.14 — must reject.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with_bg,            // Effect = Bg
        gr::as_internal,        // Security = Internal (NOT Classified/Secret)
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>(42);
    (void)bad;
    return 0;
}
