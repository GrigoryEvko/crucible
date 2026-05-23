// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-270 HS14 fixture (mint_mbarrier_wait, mismatch class: NON-CTX).
//
// Violation: mint_mbarrier_wait<MemoryScope::Cta>(42) passes a plain `int`
// where the mint's `IsExecCtx Ctx` template-parameter constraint demands a
// valid ExecCtx.  The scope (Cta) is accel-realizable precisely so the
// ctx-shape clause is the FIRST and only failure — proving the §XXI
// "first param is Ctx const&" gate is independently load-bearing (a mint is
// not a free-floating factory; it threads a context).
//
// Distinct from the arm-scope fixture (scope/accel clause vs the IsExecCtx
// constraint).
//
// Expected diagnostic: "constraints not satisfied" / IsExecCtx /
// CtxFitsMbarrierMint.

#include <crucible/fixy/Async.h>

namespace as = crucible::fixy::async;

int main() {
    auto bad = as::mint_mbarrier_wait<as::MemoryScope::Cta>(42);  // int is not an ExecCtx
    (void)bad;
    return 0;
}
