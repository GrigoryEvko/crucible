// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-121b negative fixture #2 (HS14 ≥2 floor, mint #7 of 8):
// `mint_syscall_tp_btf` IsExecCtx-half failure routed through the
// `fixy::perf::` re-export (Perf.h:168, FIXY-U-121 landing).
//
// Distinct mismatch class from fixture #1: this fixture fails the
// structural `IsExecCtx` concept (missing `row_type` + Effect
// aggregation API) rather than the Init-capability conjunct.
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsSyscallTpBtfMint" / "IsExecCtx" / "NotAnExecCtx".

#include <crucible/fixy/Perf.h>

namespace test_fixy_perf_syscall_tp_btf_not_exec_ctx {

struct NotAnExecCtx {};  // No row_type, no Effect aggregation API.

}  // namespace test_fixy_perf_syscall_tp_btf_not_exec_ctx

int main() {
    auto hub = crucible::fixy::perf::mint_syscall_tp_btf(
        test_fixy_perf_syscall_tp_btf_not_exec_ctx::NotAnExecCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
