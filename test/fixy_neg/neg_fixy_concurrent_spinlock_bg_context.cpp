// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-071 HS14 fixture #2: fixy::concurrent::SpinLock<Tag>::lock_in
// rejects when the supplied ctx owns Effect::Bg.
//
// Violation: the wrapper's `cache_tier::Hot` annotation forbids
// acquire from a background-thread context (which may also be doing
// allocation / syscall / block — incompatible with the hot-path
// budget of a spin gate's critical section).  `lock_in<BgDrainCtx>`
// must reject — `BgDrainCtx` has `Row<Bg>` and so owns
// Effect::Bg.  The requires-clause:
//
//     !CtxOwnsCapability<Ctx, Effect::Bg>
//
// fires the SFINAE rejection.  The no-ctx `lock(perm)` rail remains
// available for ConnectionPoolRuntime-style consumers that have
// already gated their members at a higher level.
//
// Expected diagnostic: constraint not satisfied (Bg context owns
// Effect::Bg).

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/concurrent/SpinLock.h>
#include <crucible/permissions/Permission.h>

struct GateTag {};

int main() {
    crucible::fixy::concurrent::SpinLock<GateTag> gate{};
    auto perm = crucible::safety::mint_permission_root<GateTag>();
    crucible::effects::BgDrainCtx bg{};

    // ✗ lock_in from a Bg context — constraint rejects.
    gate.lock_in(bg, perm);
    return 0;
}
