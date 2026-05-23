// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-257 mint_asm_grant fixture 1/2 — empty-rationale rejected.
//
// CtxFitsAsmMint<Ctx, Reason> requires `rationale_nonempty_v<Reason>`.
// Every greenfield inline-asm site MUST document WHY it drops to
// assembly; an empty rationale (`""`, size 1 = NUL only) carries no
// audit identity, so the mint refuses it.
//
// Mismatch class: rationale-engagement floor (empty string).  Distinct
// from neg_fixy_v_257_asm_non_ctx.cpp, which fires on the ExecCtx gate.
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" / "CtxFitsAsmMint" / "rationale_nonempty".

#include <crucible/fixy/Hw.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::TestRunnerCtx ctx{};
    // Should FAIL: empty rationale fails rationale_nonempty_v.
    [[maybe_unused]] auto g = ::crucible::fixy::hw::mint_asm_grant<"">(ctx);
    return 0;
}
