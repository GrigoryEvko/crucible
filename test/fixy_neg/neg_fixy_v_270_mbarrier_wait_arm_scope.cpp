// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-270 HS14 fixture (mint_mbarrier_wait, mismatch class: ARM-TRUNK).
//
// Violation: mint_mbarrier_wait<MemoryScope::Inner>(ctx) names an ARM
// inner-shareable scope (DMB ISH) for an mbarrier.try_wait — but mbarrier
// is a GPU shared-memory barrier object.  async_scope_realizable(Inner) ==
// false, so CtxFitsMbarrierMint rejects.
//
// Distinct from the non-ctx fixture on the same mint (scope clause vs the
// IsExecCtx template-parameter constraint).
//
// Expected diagnostic: "constraints not satisfied" / CtxFitsMbarrierMint.

#include <crucible/fixy/Async.h>
#include <crucible/effects/ExecCtx.h>

namespace as  = crucible::fixy::async;
namespace eff = crucible::effects;

int main() {
    constexpr eff::TestRunnerCtx ctx{};
    auto bad = as::mint_mbarrier_wait<as::MemoryScope::Inner>(ctx);  // ARM scope
    (void)bad;
    return 0;
}
