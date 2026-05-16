// ── neg_fixy_witness_floor_hot_residency (FIXY-G9 HS14) ──────────────
//
// Pins R016: a binding with Asserted-only witness on Trust /
// Reentrancy cannot satisfy `rule::R016_requires_witness_floor_v<F>`.
// HotPath::Hot residency demands at least Tested witness on those
// axes; bare bindings are rejected before the consumer admission gate.
//
// The discipline being pinned: the bare AllStrictFn (every grant
// using DefaultWitness = Asserted<UnnamedRationale>) MUST fail R016.
// The assertion below INTENTIONALLY claims R016 PASSES on the bare
// binding — when the discipline is intact, the assertion fails with
// the embedded "R016" rationale string.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cr = crucible::fixy::rule;

namespace {

using AllAssertedFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// THE DISCIPLINE BEING PINNED: R016 rejects Asserted-only Trust /
// Reentrancy witness for HotPath::Hot residency.  This assertion is
// the INVERSE — claims R016 ADMITS the bare binding.  The expected
// compile error embeds "R016" so the neg-compile driver regex matches.
static_assert(cr::R016_requires_witness_floor_v<AllAssertedFn>,
    "R016 fixture: Asserted-only witness on Trust and Reentrancy MUST "
    "be rejected before HotPath::Hot residency admission.  Build red "
    "is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
