// ── neg_fixy_rule_R015_hot_unbounded_cost (FIXY-G11 HS14) ────────────
//
// Pins R015: hot-residency bindings must carry a BOUNDED cost
// polynomial.  A binding engaging dim::Cost via `cg::cost_unknown`
// (which lowers to CostPolynomial<UINT64_MAX>) fails R015 — the
// hot-path admission gate cannot promote a binding with unbounded
// cost.
//
// The static_assert below INVERTS R015 — claims the binding passes
// R015_hot_cost_bounded_v.  Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/dim/Cost.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;

namespace {

using UnboundedCostBinding = cf::fn<int,
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
    cf::accept_default_strict_for<cd::Staleness>,
    cg::cost_unknown>;                                 // unbounded cost on Cost

// Sanity: HasCostGrant is true (the binding DOES carry a cost grant).
static_assert(cf::HasCostGrant<UnboundedCostBinding>);

// Sanity: cost polynomial is CostPolynomial<UINT64_MAX>.
static_assert(
    cf::fn_cost_polynomial_t<UnboundedCostBinding>::coeffs[0] == UINT64_MAX);

// THE DISCIPLINE: R015 rejects unbounded-cost bindings from hot
// residency.  The static_assert INVERTS the predicate.  Build red.
static_assert(cr::R015_hot_cost_bounded_v<UnboundedCostBinding>,
    "R015 fixture: a binding engaging cg::cost_unknown carries an "
    "unbounded cost polynomial (CostPolynomial<UINT64_MAX>).  Hot "
    "residency cannot admit an unbounded-cost binding — promote a "
    "named cg::cost_polynomial<...> with finite coefficients instead.  "
    "Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
