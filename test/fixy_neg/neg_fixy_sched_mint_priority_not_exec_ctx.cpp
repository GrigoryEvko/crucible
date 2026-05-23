// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-197 HS14 floor #1 of 2 for fixy::sched::mint_priority:
// the `IsExecCtx` half of `CtxFitsPriorityMint` fails when an
// arbitrary type that lacks the ExecCtx structural shape is passed
// in place of a real ExecCtx.  This catches the regression where
// `effects::testing::init()` (returns the BARE `Init` cap-struct,
// NOT an ExecCtx) — or any other non-ExecCtx — is handed directly
// to `mint_priority` without going through `mint_init_context` or
// the `ColdInitCtx` alias.
//
// Distinct from neg_fixy_sched_mint_priority_nice_out_of_range.cpp
// (which fails the Nice half of the same concept).
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsPriorityMint" / "IsExecCtx" / "is_exec_ctx_v" /
// "NotAnExecCtx" / "mint_priority".

#include <crucible/fixy/Sched.h>

namespace test_fixy_sched_mint_priority_not_exec_ctx {

struct NotAnExecCtx {};  // No row_type, no Effect aggregation API.

}  // namespace test_fixy_sched_mint_priority_not_exec_ctx

int main() {
    // Should FAIL: NotAnExecCtx is not an ExecCtx; CtxFitsPriorityMint's
    // `eff::IsExecCtx<Ctx>` conjunct rejects.
    auto p = ::crucible::fixy::sched::mint_priority<-10>(
        test_fixy_sched_mint_priority_not_exec_ctx::NotAnExecCtx{});
    (void)p;
    return 0;
}
