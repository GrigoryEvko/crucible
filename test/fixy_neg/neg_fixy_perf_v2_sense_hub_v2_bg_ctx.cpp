// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-122 negative fixture #1 (HS14 ≥2 floor, V2 sub-umbrella):
// `mint_sense_hub_v2(ctx, init)` requires-clause routed through the
// `fixy::perf::v2::` re-export (perf/V2.h, FIXY-U-122 landing).
//
// Substrate gate is
//   CtxFitsSenseHubV2Mint = IsExecCtx<Ctx>
//                         ∧ CtxOwnsCapability<Ctx, Effect::Init>.
// BgDrainCtx::row = Row<Bg, Alloc> — IsExecCtx admits but the Init
// capability is absent.
//
// CRITICAL: this fixture includes ONLY the V2 sub-umbrella; including
// `<crucible/fixy/Perf.h>` here would re-enter `namespace
// crucible::perf` with V1's conflicting `Idx` / `NUM_COUNTERS` /
// `Gauge` definitions — the alternative-build contract is what V2.h's
// HARD warning enforces.
//
// Expected diagnostic: "constraints not satisfied" /
// "mint_sense_hub_v2" / "CtxFitsSenseHubV2Mint" / "Init" /
// "row_contains".

#include <crucible/fixy/perf/V2.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    auto hub = crucible::fixy::perf::v2::mint_sense_hub_v2(
        crucible::effects::BgDrainCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
