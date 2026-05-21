// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-121b negative fixture #1 (HS14 ≥2 floor, mint #1 of 8):
// `mint_lock_contention(ctx, init)` requires-clause routed through
// the `fixy::perf::` re-export (Perf.h:120, FIXY-U-121 landing).
//
// Substrate gate is
//   CtxFitsLockContentionMint = IsExecCtx<Ctx>
//                             ∧ CtxOwnsCapability<Ctx, Effect::Init>.
// BgDrainCtx::row = Row<Bg, Alloc> — IsExecCtx admits but the Init
// capability is absent.  Short-circuit semantics name `Init` in the
// diagnostic, distinguishing this rejection path from the IsExecCtx
// half (exercised by the sibling not_exec_ctx fixture).
//
// Why the fixy::-path matters: the using-decl preserves substrate
// concept gates, but a future regression that replaces the using-decl
// with a free-function shim would bypass the concept.  This fixture
// routes the call through `crucible::fixy::perf::mint_lock_contention`
// (NOT the substrate path); a regression that loses the gate would
// leave `perf_neg/neg_lock_contention_mint_bg_ctx.cpp` green while
// THIS fixture unexpectedly compiles.
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsLockContentionMint" / "Init" / "row_contains".

#include <crucible/fixy/Perf.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    auto hub = crucible::fixy::perf::mint_lock_contention(
        crucible::effects::BgDrainCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
