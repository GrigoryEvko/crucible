// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-270 HS14 fixture (mint_mbarrier_arrive, mismatch class: ⊤ SENTINEL).
//
// Violation: mint_mbarrier_arrive<MemoryScope::System>(ctx) names the
// lattice ⊤ (full-system visibility) for an mbarrier.arrive.  System is a
// shared sentinel belonging to NEITHER trunk; mbarrier is an intra-GPU
// shared-memory construct, so a full-system mbarrier is nonsensical.
// async_scope_realizable(System) == false, so CtxFitsMbarrierMint rejects.
//
// Distinct from the arm-scope fixture (ARM trunk vs the shared ⊤ sentinel
// — both fail the accel clause, but via different scope categories).
//
// Expected diagnostic: "constraints not satisfied" / CtxFitsMbarrierMint.

#include <crucible/fixy/Async.h>
#include <crucible/effects/ExecCtx.h>

namespace as  = crucible::fixy::async;
namespace eff = crucible::effects;

int main() {
    constexpr eff::TestRunnerCtx ctx{};
    auto bad = as::mint_mbarrier_arrive<as::MemoryScope::System>(ctx);  // ⊤ sentinel
    (void)bad;
    return 0;
}
