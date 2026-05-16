// ── neg_fixy_resources_budget_lt_cost (FIXY-G12 HS14) ────────────────
//
// Pins the cross-axis cost-budget soundness check.  A binding
// declaring `cg::wallclock_budget<100>` AND `cg::cost_polynomial<500>`
// on a known Cog (Gpu) fails the check: predicted cost (500 ns) >
// budget (100 ns).  The cost-budget gate rejects.
//
// Build red is the EXPECTED outcome.

#include <crucible/cog/CogIdentity.h>
#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/dim/Cost.h>
#include <crucible/fixy/dim/Termination.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;
namespace cc = crucible::cog;

namespace {

using OverBudgetBinding = cf::fn<int,
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
    cg::cost_polynomial<500>,            // 500 ns base cost
    cg::wallclock_budget<100>>;          // 100 ns budget — TIGHT

// Sanity: both grants are detected.
static_assert(cf::HasCostGrant<OverBudgetBinding>);
static_assert(cf::wallclock_budget_v<OverBudgetBinding> == 100);

// On Gpu (mult=1), predicted cost at n=1 is 500 * 1 = 500 ns.
static_assert(cc::predicted_cost_v<OverBudgetBinding, cc::CogKind::Gpu, 1> == 500);

// THE DISCIPLINE: cost_within_budget_v rejects when predicted > budget.
// Static_assert INVERTS — claims the binding is within budget on Gpu.
static_assert(cr::cost_within_budget_v<OverBudgetBinding, cc::CogKind::Gpu, 1>,
    "Cost-budget cross-check fixture: cost_polynomial<500> predicts "
    "500 ns on Gpu at n=1, but wallclock_budget<100> caps at 100 ns.  "
    "500 > 100 — the binding cannot soundly arm warden at that "
    "deadline.  Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
