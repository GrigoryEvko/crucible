// ── neg_fixy_rule_R017_linear_alias (FIXY-G10 HS14, Followup A) ──────
//
// Pins R017's tightened semantics (Followup A): two Linear-modality
// grants engaging the SAME RegionTag NTTP are rejected.  Pre-followup
// R017 rejected ANY two Linear-modality grants on dim::Lifetime
// regardless of region tag — overconservative.  Post-followup R017
// catches exactly the structural alias.
//
// We construct a fixy::fn whose pack contains lifetime_region<0>
// TWICE — same RegionTag NTTP, both Linear-modality on dim::Lifetime.
// The fn<> engagement gate accepts (≥1 engagement per dim), but R017
// catches the alias.
//
// The assertion below INVERTS R017 — claims the pack passes R017.
// Build red is expected; "R017" appears in the canonical message.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;

namespace {

using SameRegionAliasedFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cg::lifetime_region<0>,                            // Linear #1 — tag 0
    cg::lifetime_region<0>,                            // Linear #2 — SAME tag 0 (ALIAS)
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

// THE DISCIPLINE BEING PINNED: R017 rejects a pack with two Linear-
// modality grants on the SAME RegionTag NTTP.  The assertion is the
// INVERSE.  Build red is the EXPECTED outcome.
static_assert(cr::R017_no_linear_alias_v<SameRegionAliasedFn>,
    "R017 fixture: a binding with two Linear-modality grants on the "
    "SAME RegionTag NTTP MUST be rejected — CSL frame rule forbids "
    "alias of an exclusive permission.  Build red is the EXPECTED "
    "outcome.");

}  // namespace

int main() { return 0; }
