// fixy_neg: §XXI Universal Mint Pattern rejects DIRECT use of the
//           CTAD-blocker sentinel as fixy::fn's Type argument
//           (defense-in-depth — even back-door sentinel naming fires).
//
// fixy-A4-025 negative fixture #2 (HS14 ≥2 floor: direct-sentinel
// route, distinct mismatch class from #1's CTAD route).  The CTAD
// route fires when the deduction guide picks up `fixy::fn{value}`.
// This fixture proves that a clever user attempting to BACK-DOOR the
// mint discipline by explicitly naming the sentinel type as fixy::fn's
// Type parameter ALSO fires tier-0 — the sentinel itself is the gate,
// not just the deduction guide.  Together the two fixtures prove the
// tier-0 check is robust against BOTH the deduction-guide route AND
// the explicit-Type back-door.
//
// Defense-in-depth rationale: the sentinel lives in
// `crucible::fixy::detail::ctad::` so it's not user-facing under
// normal grep, but a sufficiently determined attacker could discover
// it via header inspection.  This fixture proves they GAIN NOTHING by
// doing so — the tier-0 static_assert still fires with the §XXI
// remediation message.
//
// Reject sequence: explicit `fn<sentinel, Grants...>` instantiation →
// class body tier-0 static_assert fires (Type matches the sentinel)
// → other tiers silenced via short-circuit chain.
//
// Expected diagnostic: `fn_ctad_blocked_use_mint_fn` (sentinel CLASS
// name in error chain) OR `tier 0` (static_assert message tier prefix).

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // Direct sentinel reference: user explicitly names the sentinel
    // type as fixy::fn's Type parameter.  Even with a fully engaged
    // Grants pack (would otherwise satisfy tier-3 AllDimsEngaged),
    // tier-0 STILL fires because the sentinel detection is unconditional.
    fixy::fn<
        crucible::fixy::detail::ctad::fn_ctad_blocked_use_mint_fn_or_mint_fn_for,
        strict<D::Refinement>, strict<D::Usage>,
        strict<D::Effect>, strict<D::Security>,
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Synchronization>,
        strict<D::Regime>, strict<D::Staleness>> bad{};
    (void)bad;
    return 0;
}
