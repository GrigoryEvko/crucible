// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-121b negative fixture #2 (HS14 ≥2 floor, mint #8 of 8):
// `mint_workload_profiler` IsExecCtx-half failure routed through
// the `fixy::perf::` re-export (Perf.h:180, FIXY-U-121 landing).
// Targets the 3-arg overload — the 4-arg overload rides the same
// `CtxFitsWorkloadProfilerMint` gate by construction.
//
// Distinct mismatch class from fixture #1: this fixture fails the
// structural `IsExecCtx` concept (missing `row_type` + Effect
// aggregation API) rather than the Init-capability conjunct.
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsWorkloadProfilerMint" / "IsExecCtx" / "NotAnExecCtx".

#include <crucible/fixy/Perf.h>

namespace test_fixy_perf_workload_profiler_not_exec_ctx {

struct NotAnExecCtx {};  // No row_type, no Effect aggregation API.

}  // namespace test_fixy_perf_workload_profiler_not_exec_ctx

int main() {
    auto wp = crucible::fixy::perf::mint_workload_profiler(
        test_fixy_perf_workload_profiler_not_exec_ctx::NotAnExecCtx{},
        /*senses=*/nullptr,
        crucible::effects::testing::init());
    (void)wp;
    return 0;
}
