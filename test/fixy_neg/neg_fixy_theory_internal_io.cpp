// fixy_neg: §30.14 corpus rejects internal × IO without declassify.
//
// fixy-H-18: Theory.h's `internal_io_without_declassify` corpus entry
// detects the Internal-tier counterpart to the canonical
// `classified_io_without_declassify` shape.  Bell-LaPadula's
// no-write-down discipline applies to every non-Public Security tier
// — an `as_internal` binding (SecLevel::Internal = 2, below strict
// default Classified = 3) that emits to I/O without a declassify
// policy is the same audit-discharge violation, just lower-severity.
//
// Reject sequence: IsAccepted → IsAcceptedDirect → NotInTheoryCorpus →  // fixy-A4-023: post-H-05 chain.
// `!internal_io_without_declassify::matches<>()` evaluates false →
// IsAccepted concept fails → mint_fn signature substitution fails.
//
// Expected diagnostic: "NotInTheoryCorpus" OR the matched corpus
// entry's struct name "internal_io_without_declassify" — the
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
    //   Effect   = with_io      (I/O effect)
    //   NO declassify<Policy> grant in the pack
    // This is the Internal-tier no-write-down shape that fixy-H-18
    // added to §30.14 — must reject.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with_io,            // Effect = IO
        gr::as_internal,        // Security = Internal (NOT Classified/Secret)
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
        strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>(42);
    (void)bad;
    return 0;
}
