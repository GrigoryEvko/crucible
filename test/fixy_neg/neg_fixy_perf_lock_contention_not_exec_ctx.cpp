// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-121b negative fixture #2 (HS14 ≥2 floor, mint #1 of 8):
// `mint_lock_contention` IsExecCtx-half failure routed through the
// `fixy::perf::` re-export (Perf.h:120, FIXY-U-121 landing).
//
// Substrate gate `CtxFitsLockContentionMint<Ctx>` decomposes as
//   IsExecCtx<Ctx> ∧ CtxOwnsCapability<Ctx, Effect::Init>;
// fixture #1 exercises the second conjunct.  THIS fixture exercises
// the FIRST: a struct that fails the structural `IsExecCtx` concept
// (missing `row_type` + Effect aggregation API).  Short-circuit
// semantics name `IsExecCtx` in the diagnostic.
//
// Distinct mismatch class from fixture #1 (which names the Init
// capability).  Together they prove BOTH conjuncts hold through the
// fixy::-layer using-decl; a future regression that drops EITHER
// conjunct surfaces in exactly one fixture.
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsLockContentionMint" / "IsExecCtx" / "NotAnExecCtx".

#include <crucible/fixy/Perf.h>

namespace test_fixy_perf_lock_contention_not_exec_ctx {

struct NotAnExecCtx {};  // No row_type, no Effect aggregation API.

}  // namespace test_fixy_perf_lock_contention_not_exec_ctx

int main() {
    auto hub = crucible::fixy::perf::mint_lock_contention(
        test_fixy_perf_lock_contention_not_exec_ctx::NotAnExecCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
