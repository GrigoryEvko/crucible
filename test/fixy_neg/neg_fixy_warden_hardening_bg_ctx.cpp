// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-120b negative fixture #1 (HS14 ≥2 floor, mint #1 of 4):
// `mint_hardening(ctx, policy)` requires-clause routed through the
// `fixy::warden::` re-export (Warden.h:109, FIXY-U-120 landing).
//
// Substrate gate is
//   CtxFitsHardeningMint = IsExecCtx<Ctx> ∧ CtxOwnsCapability<Ctx, Init>.
// BgDrainCtx::row = Row<Bg, Alloc> — IsExecCtx admits but the Init
// capability is absent.  Short-circuit semantics name `Init` in the
// diagnostic, distinguishing this rejection path from the IsExecCtx
// half (exercised by the sibling non_exec_ctx fixture).
//
// Why the fixy::-path matters: the using-decl preserves substrate
// concept gates, but a future regression that replaces the using-decl
// with a free-function shim would bypass the concept.  This fixture
// routes the call through `crucible::fixy::warden::mint_hardening`
// (NOT the substrate path); a regression that loses the gate would
// leave substrate's `warden_neg/neg_hardening_mint_bg_ctx.cpp` green
// while THIS fixture unexpectedly compiles.
//
// Expected diagnostic: "no matching function" / "constraints not
// satisfied" / "CtxFitsHardeningMint" / "Init" / "row_contains".

#include <crucible/fixy/Warden.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    crucible::fixy::warden::Policy p{};
    auto applied = crucible::fixy::warden::mint_hardening(
        crucible::effects::BgDrainCtx{}, p);
    (void)applied;
    return 0;
}
