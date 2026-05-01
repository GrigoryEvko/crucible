// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-2 (#853): IsSubCtx parent-derivation witness.
//
// Violation: a fork-join helper constrained by `IsSubCtx<Child,
// Parent>` requires the child to share the parent's Cap and to have
// a Row that is a subset of the parent's.  Here HotFgCtx (Cap=Fg)
// is offered as a child of BgDrainCtx (Cap=Bg) — different caps,
// so the constraint fails.
//
// Expected diagnostic: `associated constraints are not satisfied`
// pointing at IsSubCtx.

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

template <eff::IsExecCtx Parent, eff::IsExecCtx Child>
    requires eff::IsSubCtx<Child, Parent>
constexpr void fork_child(Parent const&, Child const&) noexcept {}

int main() {
    eff::BgDrainCtx parent;
    eff::HotFgCtx   child;       // Cap=Fg vs parent Cap=Bg
    fork_child(parent, child);   // IsSubCtx<HotFgCtx, BgDrainCtx> = false
    return 0;
}
