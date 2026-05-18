// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 4 for fixy-A3-027 (ExecCtx Progress axis).
//
// Premise: the new 8th axis (Progress) is gated by the
// `IsProgressClass<Progress>` clause inside the `WellFormedExecCtxAxes`
// concept (fixy-A3-020 hoist).  A typo at the Progress slot — e.g.
// passing `int` instead of one of `ctx_progress::{MayDiverge,
// Terminating, Productive, Bounded}` — must fail at the requires-clause
// with ONE diagnostic, not cascade through the class body.
//
// Symmetric witness to neg_exec_ctx_well_formed_axes_workload_typo —
// proves the trailing-axis position also short-circuits cleanly.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "constraints not satisfied" / "WellFormedExecCtxAxes" /
//   "IsProgressClass" / "fixy-A3-027".

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

int main() {
    using BadCtx = eff::ExecCtx<
        eff::ctx_cap::Fg,
        eff::ctx_numa::Any,
        eff::ctx_alloc::Unbound,
        eff::ctx_heat::Cold,
        eff::ctx_resid::DRAM,
        eff::Row<>,
        eff::ctx_workload::Unspecified,
        int                                  // NOT a progress class — fixy-A3-027
    >;
    BadCtx bad{};
    (void)bad;
    return 0;
}
