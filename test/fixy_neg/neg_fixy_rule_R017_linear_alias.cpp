// ── neg_fixy_rule_R017_linear_alias (FIXY-G10 HS14) ──────────────────
//
// Pins R017: a binding pack must not contain two-or-more Linear-
// modality grants on the same dim::Lifetime.  Linear modality (CSL
// frame rule) is one-shot consume-and-produce; two parallel consumers
// of the same Permission tag is structurally illegal.
//
// We construct a fixy::fn whose pack contains TWO lifetime_region
// grants (different tags but both Linear-modality on dim::Lifetime).
// The fn<> engagement gate accepts this (it only checks ≥1 engagement
// per dim), but R017 catches the alias.
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

// Phantom region tags.
struct region_a {};
struct region_b {};

using DualLinearFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cg::lifetime_region<0>,                            // Linear #1
    cg::lifetime_region<1>,                            // Linear #2 — ALIAS
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
// modality grants on dim::Lifetime.  The assertion is the INVERSE.
static_assert(cr::R017_no_linear_alias_v<DualLinearFn>,
    "R017 fixture: a binding with two Linear-modality grants on "
    "dim::Lifetime MUST be rejected — CSL frame rule forbids alias "
    "of an exclusive permission.  Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
