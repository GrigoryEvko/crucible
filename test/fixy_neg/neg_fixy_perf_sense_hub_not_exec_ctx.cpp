// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-121b negative fixture #2 (HS14 ≥2 floor, mint #5 of 8):
// `mint_sense_hub` IsExecCtx-half failure routed through the
// `fixy::perf::` re-export (Perf.h:152, FIXY-U-121 landing).
//
// Distinct mismatch class from fixture #1: this fixture fails the
// structural `IsExecCtx` concept (missing `row_type` + Effect
// aggregation API) rather than the Init-capability conjunct.
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsSenseHubMint" / "IsExecCtx" / "NotAnExecCtx".

#include <crucible/fixy/Perf.h>

namespace test_fixy_perf_sense_hub_not_exec_ctx {

struct NotAnExecCtx {};  // No row_type, no Effect aggregation API.

}  // namespace test_fixy_perf_sense_hub_not_exec_ctx

int main() {
    auto hub = crucible::fixy::perf::mint_sense_hub(
        test_fixy_perf_sense_hub_not_exec_ctx::NotAnExecCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
