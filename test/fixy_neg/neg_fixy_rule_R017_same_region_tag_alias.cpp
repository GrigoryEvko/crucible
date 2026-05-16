// ── neg_fixy_rule_R017_same_region_tag_alias (Followup A HS14) ───────
//
// Per-RegionTag precision of R017.  Followup A tightens R017 to detect
// SAME-region-tag aliasing only.  This fixture pins the structural
// soundness: a binding with two Linear-modality grants on the SAME
// RegionTag NTTP is the canonical alias case.
//
// The fixture uses an evidenced variant `lifetime_region_e<0, W>` on
// one side and bare `lifetime_region<0>` on the other — same tag, two
// distinct grant types.  Both engage dim::Lifetime via Linear modality
// pointing at RegionTag = 0; R017's per-RegionTag check catches the
// alias regardless of witness variation.
//
// Build red is the EXPECTED outcome — the static_assert inverts R017.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>
#include <crucible/safety/witness/Witness.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;
namespace sw = crucible::safety::witness;

namespace {

using EvidencedAndBareSameTagFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cg::lifetime_region<0>,                                 // bare tag 0
    cg::lifetime_region_e<0, sw::Tested<42>>,               // evidenced tag 0 — ALIAS
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

// INVERSE assertion — claims the same-tag alias passes R017.  R017
// rejects, the static_assert fires, build red.
static_assert(cr::R017_no_linear_alias_v<EvidencedAndBareSameTagFn>,
    "R017 fixture (same-tag): bare and evidenced lifetime_region grants "
    "engaging the SAME RegionTag NTTP must be rejected — witness "
    "variation doesn't disambiguate; the RegionTag NTTP is the identity "
    "axis.  Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
