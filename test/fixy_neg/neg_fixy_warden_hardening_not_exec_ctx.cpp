// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-120b negative fixture #2 (HS14 ≥2 floor, mint #1 of 4):
// `mint_hardening` IsExecCtx-half failure routed through the
// `fixy::warden::` re-export (Warden.h:109, FIXY-U-120 landing).
//
// Substrate gate `CtxFitsHardeningMint<Ctx>` decomposes as
// `IsExecCtx<Ctx> ∧ CtxOwnsCapability<Ctx, Init>`; fixture #1 exercises
// the second conjunct.  THIS fixture exercises the FIRST: a struct
// that fails the structural `IsExecCtx` concept (missing
// `row_type` + Effect aggregation API).  Short-circuit semantics name
// `IsExecCtx` in the diagnostic.
//
// Distinct mismatch class from fixture #1 (which names the Init
// capability).  Together they prove BOTH conjuncts hold through the
// fixy::-layer using-decl; a future regression that drops EITHER
// conjunct surfaces in exactly one fixture.
//
// Expected diagnostic: "no matching function" / "constraints not
// satisfied" / "CtxFitsHardeningMint" / "IsExecCtx" / "NotAnExecCtx".

#include <crucible/fixy/Warden.h>

namespace test_fixy_warden_hardening_not_exec_ctx {

struct NotAnExecCtx {};  // No row_type, no Effect aggregation API.

}  // namespace test_fixy_warden_hardening_not_exec_ctx

int main() {
    crucible::fixy::warden::Policy p{};
    auto applied = crucible::fixy::warden::mint_hardening(
        test_fixy_warden_hardening_not_exec_ctx::NotAnExecCtx{}, p);
    (void)applied;
    return 0;
}
