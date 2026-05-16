// ── neg_fixy_cost_witness_floor_calibration (FIXY-G11 HS14) ──────────
//
// Pins the combined R015 + R016 demand: hot-tier cost grades require
// a Tested witness (per-Cog calibration evidence).  A bare
// `cg::cost_polynomial<...>` carries the DefaultWitness (Asserted),
// which is BELOW the Tested floor.  The combined gate rejects.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/Witness.h>
#include <crucible/fixy/dim/Cost.h>
#include <crucible/safety/witness/Witness.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;
namespace sw = crucible::safety::witness;

namespace {

using AssertedCostBinding = cf::fn<int,
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
    cg::cost_polynomial<5, 3>>;                        // bare → Asserted witness

// Combined gate: R015 must pass AND the cost-axis witness must reach
// the Tested floor.  Bare cost_polynomial has DefaultWitness =
// Asserted, so the Tested floor fails.
template <typename F>
concept R015_Plus_TestedCostFloor =
    cr::R015_hot_cost_bounded_v<F> &&
    cf::FnWitnessAtLeast<F, cd::Cost,
                         sw::Tested<crucible::safety::diag::UnnamedTestId>>;

// Sanity: R015 alone passes (cost IS bounded).
static_assert(cr::R015_hot_cost_bounded_v<AssertedCostBinding>);

// INVERSE assertion — claims the combined gate passes; build red.
static_assert(R015_Plus_TestedCostFloor<AssertedCostBinding>,
    "Combined R015+R016 fixture: a bare `cg::cost_polynomial<...>` "
    "carries DefaultWitness (Asserted) on dim::Cost.  Hot-tier "
    "promotion demands BOTH bounded cost AND Tested-or-stronger "
    "witness (per-Cog calibration evidence).  Use "
    "`cg::cost_polynomial_e<Tested<calib_run_id>, ...>` to satisfy.  "
    "Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
