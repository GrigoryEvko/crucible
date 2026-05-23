// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-257 mint_asm_grant fixture 2/2 — non-ExecCtx rejected.
//
// Every §XXI ctx-bound mint constrains its trailing parameter with
// `effects::IsExecCtx Ctx`.  Passing a bare `int` (not an ExecCtx)
// fails the constraint, so no overload matches.
//
// Mismatch class: ExecCtx gate (CtxFitsHwGrant floor).  Distinct from
// neg_fixy_v_257_asm_empty_rationale.cpp, which fires on the rationale.
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" / "IsExecCtx".

#include <crucible/fixy/Hw.h>

int main() {
    int not_a_ctx = 0;
    // Should FAIL: int does not satisfy effects::IsExecCtx.
    [[maybe_unused]] auto g =
        ::crucible::fixy::hw::mint_asm_grant<"valid rationale">(not_a_ctx);
    return 0;
}
