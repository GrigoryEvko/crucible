// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-269 HS14 fixture #2 (mismatch class: CROSS-TRUNK scope×arch).
//
// Violation: mint_scoped_fence<MemoryScope::Gpu, BarrierArch::Arm>(ctx)
// pairs an accel-trunk (GPU device `.gpu`) visibility scope with the ARM
// DMB fence dialect.  The MemoryScopeLattice marks the accel and ARM trunks
// mutually incomparable — the ARM fence dialect cannot realize a GPU device
// scope.  scope_arch_trunk_consistent(Gpu, Arm) is false, so
// CtxFitsScopedFenceMint<Ctx, Gpu, Arm> rejects.  This is the GRANT-mint-
// boundary mirror of CollisionCatalog V402 (the Fn-composition rule).
//
// Distinct from fixture #1 (bottom-scope): here the scope is a real, non-⊥
// scope, but the (scope, arch) PAIR is dialect-incoherent.
//
// Expected diagnostic: GCC's "constraints not satisfied" pointing at
// CtxFitsScopedFenceMint.

#include <crucible/fixy/Hw.h>
#include <crucible/effects/ExecCtx.h>

namespace hw  = crucible::fixy::hw;
namespace eff = crucible::effects;

int main() {
    constexpr eff::TestRunnerCtx ctx{};
    // accel scope on the ARM fence dialect — cross-trunk, gate rejects.
    auto bad = hw::mint_scoped_fence<hw::MemoryScope::Gpu, hw::BarrierArch::Arm>(ctx);
    (void)bad;
    return 0;
}
