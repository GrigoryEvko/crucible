// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #4 of 4 for fixy-A3-027 (ExecCtx Progress axis).
//
// Premise: even though Progress defaults to Terminating (the F* common-
// case default), a builder chain CAN explicitly demote it to MayDiverge
// AFTER advancing to Hot tier — `with_progress<MayDiverge>()` is a
// valid builder transition in isolation (the lattice admits it), but
// the returned type fires the class-body
// static_assert(heat_progress_coherent_v<>) because the resulting
// (Heat=Hot, Progress=MayDiverge) pair is internally contradictory.
//
// This is the LATER-DEMOTION trap, distinct from fixture #1's direct
// instantiation: it witnesses that the coherence rule re-checks at
// every builder return, not just at root construction.  A half-broken
// impl that only validated at root would silently admit
// `.with_heat<Hot>().with_progress<MayDiverge>()`.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "heat_progress_coherent" /
//   "Heat × Progress" / "fixy-A3-027".

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

int main() {
    constexpr eff::ExecCtx<> ctx{};
    // Step 1: advance to Hot (legal — default Progress=Terminating
    // satisfies Heat × Progress).
    auto hot = ctx.template with_residency<eff::ctx_resid::L1>()
                  .template with_heat<eff::ctx_heat::Hot>();
    // Step 2: demote Progress to MayDiverge — fires the class-body
    // static_assert on the returned ExecCtx<..., Hot, ..., MayDiverge>.
    auto bad = hot.template with_progress<eff::ctx_progress::MayDiverge>();
    (void)bad;
    return 0;
}
