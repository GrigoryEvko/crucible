// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 4 for fixy-A3-027 (ExecCtx Progress axis).
//
// Premise: heat_progress_coherent_v<Heat, Progress> is the new
// cross-axis coherence rule introduced by the 8th axis.  Hot-path
// tier paired with Progress=MayDiverge is internally contradictory
// — a hot-path function that may never return blocks the foreground
// thread.  The class-body static_assert in ExecCtx<> pins this at
// instantiation, mirroring the existing Heat × Resid pattern.
//
// This fixture instantiates the bad pair DIRECTLY (skipping the
// HotFgCtx alias which explicitly threads Terminating) and witnesses
// that the static_assert(heat_progress_coherent_v<...>) fires.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "heat_progress_coherent" /
//   "Heat × Progress" / "fixy-A3-027".

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

int main() {
    using BadCtx = eff::ExecCtx<
        eff::ctx_cap::Fg,
        eff::ctx_numa::Any,
        eff::ctx_alloc::Stack,
        eff::ctx_heat::Hot,                  // Hot tier
        eff::ctx_resid::L1,                  // L1 satisfies Heat × Resid
        eff::Row<>,
        eff::ctx_workload::Unspecified,
        eff::ctx_progress::MayDiverge        // but MayDiverge — incoherent
    >;
    BadCtx bad{};
    (void)bad;
    return 0;
}
