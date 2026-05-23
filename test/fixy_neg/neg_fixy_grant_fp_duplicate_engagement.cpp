// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-092 HS14 fixture 2/2.  A Grants pack engaging FpMode TWICE
// (here: `fp_strict_ieee` AND `with_fp_rounding<RoundToNearestEven>`)
// signals an authoring contradiction — the binding claims both "every
// sub-axis at IEEE strict default" AND "Rounding pinned to RTE
// specifically" — which the resolver cannot disambiguate without a
// silent first-wins rule.  Reject.h's `UniqueEngagementPerAxis`
// concept (FIXY-AUDIT-A3) must fire on the duplicate FpMode-routing.
//
// Mismatch class for HS14 audit: duplicate-engagement (distinct from
// the NTTP-enum-type mismatch fixture's template-id-formation
// rejection — both class paths protect the SAME engagement walk but
// at orthogonal stages: NTTP-mismatch rejects BEFORE the grant
// reaches the pack; duplicate-engagement rejects AFTER all grants
// are individually well-formed but their pack as a whole engages an
// axis twice).
//
// Architectural intent: the V-092 design choice to expose BOTH a
// per-sub-axis parametric grant AND an aggregate fp_strict_ieee
// covers two distinct use cases — explicit per-axis pinning vs
// blanket strict-IEEE — but they MUST NOT be combined in one
// binding's Grants pack.  The aggregate and any per-axis form route
// to the same DimensionAxis::FpMode; the engagement walk catches the
// double-engage and emits the named FixyDuplicate_FpMode diagnostic
// per Reject.h:213.
//
// Concrete bug-class this catches: a contributor reads V-092's doc-
// block, opts for `fp_strict_ieee` for the general case, then on
// review adds `with_fp_rounding<RoundToNearestEven>` "for
// documentation" — silently lossy under any first-wins resolver
// rule, structurally rejected here.
//
// Expected diagnostic: "UniqueEngagementPerAxis" OR
// "FixyDuplicate_FpMode" — the satisfaction-failure chain names
// either the multiplicity concept or the per-axis duplicate tag.

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Fp.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
namespace sf   = crucible::safety;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 28-element pack: 27 distinct axes engaged with strict markers +
    // BOTH fp_strict_ieee AND with_fp_rounding<RTE> covering FpMode
    // twice.  The duplicate FpMode engagement is the load-bearing
    // rejection cause; every other axis is uniquely engaged
    // (including SyscallSurface, the V-097 axis, and the 5 V-238 hazard
    // axes ControlFlow/CallShape/StackUse/GlobalState/Stdio).
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>,
        strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
        strict<D::Synchronization>, strict<D::Regime>,
        strict<D::SyscallSurface>,
        strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>,
        strict<D::GlobalState>, strict<D::Stdio>,
        gr::fp_strict_ieee /* FpMode #1 */,
        gr::with_fp_rounding<sf::FpRounding::RoundToNearestEven>
                              /* FpMode #2 — duplicate */>(42);
    (void)bad;
    return 0;
}
