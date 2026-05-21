// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-121b negative fixture #1 (HS14 ≥2 floor, mint #4 of 8):
// `mint_sched_tp_btf(ctx, init)` requires-clause routed through the
// `fixy::perf::` re-export (Perf.h:144, FIXY-U-121 landing).
//
// Substrate gate is
//   CtxFitsSchedTpBtfMint = IsExecCtx<Ctx>
//                         ∧ CtxOwnsCapability<Ctx, Effect::Init>.
// BgDrainCtx::row = Row<Bg, Alloc> — IsExecCtx admits but the Init
// capability is absent.
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsSchedTpBtfMint" / "Init" / "row_contains".

#include <crucible/fixy/Perf.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    auto hub = crucible::fixy::perf::mint_sched_tp_btf(
        crucible::effects::BgDrainCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
