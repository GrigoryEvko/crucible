// fixy_neg: §30.14 corpus rejects classified × stale_to<N> without declassify.
//
// HS14 floor for FIXY-AUDIT-G6 (Theory.h corpus expansion, Agent B).
// Theory.h's `staleness_secret_without_declassify` corpus entry detects
// the stale-replay information-flow shape: a binding engaging both
// `as_secret` (Security=Secret) AND `stale_to<TauMax>` (Staleness ≠
// Fresh) without interposing `declassify<Policy>` to authorize the
// temporal flow.
//
// Cite: Sabelfeld-Sands 2009 / Hunt-Sands 2008 (fixy-CR-16: Andrysco-
// 2015 was a misattributed paper about FP timing channels and has been
// replaced).  A classified value reachable through a non-Fresh
// staleness window without a freshness-discharging policy is a failed
// erasure: the replay-window keeps observable what Hunt-Sands' erasure
// semantics would require be forgotten.
//
// Distinct from R011/S010 (CollisionCatalog marks_ct opt-in): this
// catches the pattern at the type level WITHOUT requiring an external
// `marks_ct` trait specialization.  Defense-in-depth.
//
// Reject sequence: IsAccepted → IsAcceptedDirect → NotInTheoryCorpus →  // fixy-A4-023: post-H-05 chain.
// `!staleness_secret_without_declassify::matches<>()` evaluates false →
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
    // 20-axis pack engaging:
    //   Security  = as_secret      (classified value)
    //   Staleness = stale_to<100>  (replay window of 100 units)
    //   NO declassify<Policy> grant in the pack
    // This is the Sabelfeld-Sands stale-replay shape.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
        gr::as_secret,                          // Security = Secret
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Synchronization>,
        strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>,
        gr::stale_to<100>>(42);                 // Staleness ≠ Fresh
    (void)bad;
    return 0;
}
