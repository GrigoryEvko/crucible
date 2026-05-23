// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-269 HS14 fixture #1 (mismatch class: BOTTOM-scope / "no fence").
//
// Violation: mint_scoped_fence<MemoryScope::Thread, Arch>(ctx) names the
// lattice ⊥ scope (Thread — visible only to the issuing thread, satisfies
// no cross-thread gate), which the task spec calls "Scope == None".  You do
// NOT mint a scoped FENCE for thread-local-only visibility — there is no
// fence to emit.  CtxFitsScopedFenceMint<Ctx, Thread, Arch> is false on its
// `Scope != MemoryScope::Thread` clause (mirrors the tsc gate's
// `Mode != NotAllowed`), so the mint's requires-clause rejects.
//
// Distinct from fixture #2 (cross-trunk): here the scope is well-typed but
// the floor sentinel; there the (scope, arch) PAIR is incoherent.
//
// Expected diagnostic: GCC's "constraints not satisfied" pointing at
// CtxFitsScopedFenceMint.

#include <crucible/fixy/Hw.h>
#include <crucible/effects/ExecCtx.h>

namespace hw  = crucible::fixy::hw;
namespace eff = crucible::effects;

int main() {
    constexpr eff::TestRunnerCtx ctx{};
    // Thread (⊥) is the no-cross-thread-visibility sentinel — gate rejects.
    auto bad = hw::mint_scoped_fence<hw::MemoryScope::Thread, hw::BarrierArch::Arm>(ctx);
    (void)bad;
    return 0;
}
