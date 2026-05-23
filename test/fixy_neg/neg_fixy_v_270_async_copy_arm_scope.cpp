// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-270 HS14 fixture (mint_async_copy, mismatch class: ARM-TRUNK scope).
//
// Violation: mint_async_copy<2, MemoryScope::Inner, 16>(ctx) targets an
// ARM-host shareability scope (DMB ISH) for a cp.async/TMA fill — but
// cp.async is a GPU-only construct that fills shared memory at an
// accelerator scope.  async_scope_realizable(Inner) == false (Inner is the
// ARM trunk, not accel), so CtxFitsAsyncCopyMint rejects.
//
// Distinct from the zero-stages fixture (which exercises the Stages>=1
// clause on the SAME mint); here the Scope/accel clause fires.
//
// Expected diagnostic: "constraints not satisfied" / CtxFitsAsyncCopyMint.

#include <crucible/fixy/Async.h>
#include <crucible/effects/ExecCtx.h>

namespace as  = crucible::fixy::async;
namespace eff = crucible::effects;

int main() {
    constexpr eff::TestRunnerCtx ctx{};
    auto bad = as::mint_async_copy<2, as::MemoryScope::Inner, 16>(ctx);  // ARM scope
    (void)bad;
    return 0;
}
