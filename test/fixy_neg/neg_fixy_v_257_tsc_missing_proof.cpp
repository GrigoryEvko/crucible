// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-257 mint_tsc_grant fixture 2/2 — missing CpuPinProof rejected.
//
// mint_tsc_grant's signature is `(Ctx const&, CpuPinProof)`.  The
// CpuPinProof argument is the affinity witness: a TSC read on an
// unpinned thread can migrate cores and read a different counter.
// Calling without it leaves no matching overload.
//
// Mismatch class: missing proof-token argument (arity / overload
// resolution).  Distinct from neg_fixy_v_257_tsc_not_allowed.cpp, which
// fires on the strict-default posture.
//
// Expected diagnostic: "no matching function" / "too few arguments" /
// "CpuPinProof".

#include <crucible/fixy/Hw.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::TestRunnerCtx ctx{};
    namespace hw = ::crucible::fixy::hw;
    // Should FAIL: no CpuPinProof argument supplied.
    [[maybe_unused]] auto g = hw::mint_tsc_grant<hw::TscMode::SerializedPinned>(ctx);
    return 0;
}
