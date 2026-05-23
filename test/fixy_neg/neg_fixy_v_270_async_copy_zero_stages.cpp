// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-270 HS14 fixture (mint_async_copy, mismatch class: DEGENERATE Stages).
//
// Violation: mint_async_copy<0, MemoryScope::Cta, 16>(ctx) requests a
// 0-stage pipeline — there is nothing to overlap, so the async-copy
// pipeline is degenerate.  CtxFitsAsyncCopyMint's `Stages >= 1` clause
// rejects.  The scope (Cta) is accel-realizable precisely so the Stages
// clause is the FIRST and only failure — proving it is independently
// load-bearing.
//
// Distinct from the arm-scope fixture (Scope/accel clause on the same mint).
//
// Expected diagnostic: "constraints not satisfied" / CtxFitsAsyncCopyMint.

#include <crucible/fixy/Async.h>
#include <crucible/effects/ExecCtx.h>

namespace as  = crucible::fixy::async;
namespace eff = crucible::effects;

int main() {
    constexpr eff::TestRunnerCtx ctx{};
    auto bad = as::mint_async_copy<0, as::MemoryScope::Cta, 16>(ctx);  // 0 stages
    (void)bad;
    return 0;
}
