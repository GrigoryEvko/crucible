// fixy_neg: §30.14 corpus rejects classified × IO without declassify.
//
// HS14 floor for FIXY-AUDIT-B1.  Theory.h's
// `classified_io_without_declassify` corpus entry detects the
// canonical implicit-flow shape: a binding engaging both `as_secret`
// (Security=Secret) AND `with_io` (Effect=IO) without interposing
// `declassify<Policy>` to discharge the audit trail.
//
// Reject sequence: IsAccepted → IsAcceptedDirect → NotInTheoryCorpus →  // fixy-A4-023: post-H-05 chain.
// `!classified_io_without_declassify::matches<>()` evaluates false →
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
    //   Effect   = with_io    (I/O effect)
    //   NO declassify<Policy> grant in the pack
    // This is the §30.14 implicit-flow shape — must reject.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with_io,            // Effect = IO
        gr::as_secret,          // Security = Secret
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
        strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>>(42);
    (void)bad;
    return 0;
}
