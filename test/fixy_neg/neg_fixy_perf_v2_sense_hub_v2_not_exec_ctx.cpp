// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-122 negative fixture #2 (HS14 ≥2 floor, V2 sub-umbrella):
// `mint_sense_hub_v2` IsExecCtx-half failure routed through the
// `fixy::perf::v2::` re-export (perf/V2.h, FIXY-U-122 landing).
//
// Distinct mismatch class from fixture #1: this fixture fails the
// structural `IsExecCtx` concept (missing `row_type` + Effect
// aggregation API) rather than the Init-capability conjunct.
//
// Expected diagnostic: "constraints not satisfied" /
// "mint_sense_hub_v2" / "CtxFitsSenseHubV2Mint" / "IsExecCtx" /
// "NotAnExecCtx".

#include <crucible/fixy/perf/V2.h>

namespace test_fixy_perf_v2_sense_hub_v2_not_exec_ctx {

struct NotAnExecCtx {};  // No row_type, no Effect aggregation API.

}  // namespace test_fixy_perf_v2_sense_hub_v2_not_exec_ctx

int main() {
    auto hub = crucible::fixy::perf::v2::mint_sense_hub_v2(
        test_fixy_perf_v2_sense_hub_v2_not_exec_ctx::NotAnExecCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
