// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-121b negative fixture #1 (HS14 ≥2 floor, mint #8 of 8):
// `mint_workload_profiler(ctx, senses, init)` requires-clause routed
// through the `fixy::perf::` re-export (Perf.h:180, FIXY-U-121
// landing).  Targets the 3-arg overload — the 4-arg overload rides
// the same `CtxFitsWorkloadProfilerMint` gate, so this fixture is
// sufficient to witness the using-decl identity by construction.
//
// Substrate gate is
//   CtxFitsWorkloadProfilerMint = IsExecCtx<Ctx>
//                               ∧ CtxOwnsCapability<Ctx, Effect::Init>.
// BgDrainCtx::row = Row<Bg, Alloc> — IsExecCtx admits but the Init
// capability is absent.  The profiler's ctor reads a borrowed
// Senses* to capture a baseline snapshot from an Init-row resource;
// constructing it outside Init is unsound.
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsWorkloadProfilerMint" / "Init" / "row_contains".

#include <crucible/fixy/Perf.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    auto wp = crucible::fixy::perf::mint_workload_profiler(
        crucible::effects::BgDrainCtx{},
        /*senses=*/nullptr,
        crucible::effects::testing::init());
    (void)wp;
    return 0;
}
