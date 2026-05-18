// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-020 audit fixture (2/2): pins the WellFormedExecCtxAxes
// concept's coverage of NON-leading axes.  The companion fixture
// (neg_exec_ctx_int_typo_clean_diag.cpp) typos the FIRST axis
// (Cap = int) and demonstrates short-circuit at the IsCapType atom.
// This fixture typos the LAST axis (Workload = int) — proving the
// concept conjunction evaluates every atom in order and the
// IsWorkloadHint atom fires when all upstream atoms pass.
//
// Without this asymmetry pin, a regression that moved IsWorkloadHint
// out of WellFormedExecCtxAxes (or that reordered atoms so a typo at
// the trailing axis was masked by an earlier-failing axis) would
// pass CI silently.  The fixture's failure is the witness that the
// concept catches typo at every axis position, not just the first.
//
// Expected diagnostic: "constraints not satisfied" /
// "WellFormedExecCtxAxes" / "IsWorkloadHint" / "fixy-A3-020"

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>

namespace eff = ::crucible::effects;

// fixy-A3-020: every leading axis is the documented default; the
// SINGLE deviation is `int` at the Workload position.  Concept
// conjunction proceeds left-to-right through all 7 axis recognitions
// and the cap-permitted-row Subrow atom; the only atom that fails
// is `IsWorkloadHint<int>`.  The fixture's static_assert pins the
// requires-clause refusal — without the WellFormedExecCtxAxes hoist
// this fired a body static_assert chain; after the hoist it must
// fail at the class-template head.
using BadCtx = eff::ExecCtx<
    ::crucible::effects::ctx_cap::Fg,
    ::crucible::effects::ctx_numa::Any,
    ::crucible::effects::ctx_alloc::Unbound,
    ::crucible::effects::ctx_heat::Cold,
    ::crucible::effects::ctx_resid::DRAM,
    eff::Row<>,
    int                          // ← Workload typo, must reject
>;

[[maybe_unused]] static constexpr BadCtx witness{};

int main() { return 0; }
