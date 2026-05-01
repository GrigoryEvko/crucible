// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT (#852): Heat × Resid cross-axis coherence.
//
// Violation: ExecCtx<..., Heat=Hot, Resid=DRAM, ...> claims a hot-
// path tier paired with DRAM residency.  Hot-path data must live in
// L1/L2 — DRAM is 200-300 cycles, defeating the tier promise.  The
// heat_resid_coherent_v invariant catches this at the ExecCtx
// instantiation point.
//
// Expected diagnostic: the `static_assert(heat_resid_coherent_v<…>)`
// clause in ExecCtx fires at instantiation, citing the cross-axis
// rule.

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

int main() {
    using BadCtx = eff::ExecCtx<
        eff::ctx_cap::Fg,
        eff::ctx_numa::Any,
        eff::ctx_alloc::Stack,
        eff::ctx_heat::Hot,                             // Hot tier
        eff::ctx_resid::DRAM,                           // but DRAM-resident — incoherent
        eff::Row<>
    >;
    BadCtx bad{};
    (void)bad;
    return 0;
}
