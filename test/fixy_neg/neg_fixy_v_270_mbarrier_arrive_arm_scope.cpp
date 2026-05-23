// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-270 HS14 fixture (mint_mbarrier_arrive, mismatch class: ARM-TRUNK).
//
// Violation: mint_mbarrier_arrive<MemoryScope::Outer>(ctx) names an ARM
// outer-shareable scope (DMB OSH) for an mbarrier.arrive — but mbarrier is
// a GPU shared-memory barrier object (.shared::cta / .shared::cluster).
// async_scope_realizable(Outer) == false, so CtxFitsMbarrierMint rejects.
//
// Distinct from the system-scope fixture on the same mint (which exercises
// the shared ⊤ sentinel rather than the ARM trunk).
//
// Expected diagnostic: "constraints not satisfied" / CtxFitsMbarrierMint.

#include <crucible/fixy/Async.h>
#include <crucible/effects/ExecCtx.h>

namespace as  = crucible::fixy::async;
namespace eff = crucible::effects;

int main() {
    constexpr eff::TestRunnerCtx ctx{};
    auto bad = as::mint_mbarrier_arrive<as::MemoryScope::Outer>(ctx);  // ARM scope
    (void)bad;
    return 0;
}
