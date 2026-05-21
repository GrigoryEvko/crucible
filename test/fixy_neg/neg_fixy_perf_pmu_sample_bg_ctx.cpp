// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-121b negative fixture #1 (HS14 ≥2 floor, mint #2 of 8):
// `mint_pmu_sample(ctx, init)` requires-clause routed through the
// `fixy::perf::` re-export (Perf.h:128, FIXY-U-121 landing).
//
// Substrate gate is
//   CtxFitsPmuSampleMint = IsExecCtx<Ctx>
//                        ∧ CtxOwnsCapability<Ctx, Effect::Init>.
// BgDrainCtx::row = Row<Bg, Alloc> — IsExecCtx admits but the Init
// capability is absent.  Short-circuit semantics name `Init` in the
// diagnostic.
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsPmuSampleMint" / "Init" / "row_contains".

#include <crucible/fixy/Perf.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    auto hub = crucible::fixy::perf::mint_pmu_sample(
        crucible::effects::BgDrainCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
