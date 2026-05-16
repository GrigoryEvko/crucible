// ── neg_fixy_resources_bgworker_without_budget (FIXY-G12 HS14) ───────
//
// Pins R014: a BG worker binding observable via warden MUST carry
// BOTH bounded_alloc AND wallclock_budget.  A binding with
// bounded_alloc but NO wallclock_budget fails R014 — the warden can
// observe but cannot bound deadline.
//
// Build red is the EXPECTED outcome.

#include <crucible/effects/Capabilities.h>
#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/dim/Termination.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;

namespace {

using BgWithAllocOnly = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cg::with<crucible::effects::Effect::Bg,
             crucible::effects::Effect::Alloc>,        // BG context
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cg::observability_visible,                          // observable
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>,
    cg::bounded_alloc<8192>>;                           // alloc bound; NO budget

// Sanity: bounded_alloc engaged.
static_assert(cf::bounded_alloc_v<BgWithAllocOnly> == 8192);

// Sanity: NO wallclock_budget engaged → sentinel.
static_assert(cf::wallclock_budget_v<BgWithAllocOnly> == UINT64_MAX);

// THE DISCIPLINE: R014 rejects when wallclock_budget is missing.
// Static_assert INVERTS — claims R014 passes.
static_assert(cr::R014_bg_observable_bounded_v<BgWithAllocOnly>,
    "R014 fixture: a BG worker engaging observability_visible must "
    "carry BOTH bounded_alloc AND wallclock_budget.  Missing "
    "wallclock_budget makes the warden's bound check impossible.  "
    "Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
