// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-2 (#853): CtxOwnsCapability per-effect-atom witness.
//
// Violation: a function that performs IO must verify the surrounding
// Ctx authorizes Effect::IO via `CtxOwnsCapability<Ctx, Effect::IO>`.
// Here BgDrainCtx (row = {Bg, Alloc}) is offered to a function
// requiring IO authorisation; IO is not in BgDrain's row, so the
// constraint fails.
//
// Expected diagnostic: `associated constraints are not satisfied`
// pointing at CtxOwnsCapability / row_contains_v.

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

template <eff::IsExecCtx Ctx>
    requires eff::CtxOwnsCapability<Ctx, eff::Effect::IO>
constexpr void perform_io(Ctx const&) noexcept {}

int main() {
    eff::BgDrainCtx bg;          // row = {Bg, Alloc} — no IO
    perform_io(bg);
    return 0;
}
