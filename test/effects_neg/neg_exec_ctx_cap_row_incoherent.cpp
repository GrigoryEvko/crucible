// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT (#852): cap-permitted-row coherence invariant.
//
// Violation: ExecCtx<ctx_cap::Fg, ..., Row<Effect::Bg>> claims a
// foreground context that nevertheless authorizes a Bg-effect row.
// This is the central soundness invariant ExecCtx encodes: a Cap
// publishes its permitted_row, and the ExecCtx Row must be a Subrow
// of it.  Foreground (Fg) permits only Row<>; promoting to a Bg
// effect requires holding the Bg cap context.
//
// Expected diagnostic: the `static_assert(Subrow<Row, …>)` clause in
// ExecCtx fires at instantiation, naming the cap-permitted-row
// invariant.

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

int main() {
    using BadCtx = eff::ExecCtx<
        eff::ctx_cap::Fg,                              // foreground
        eff::ctx_numa::Any,
        eff::ctx_alloc::Unbound,
        eff::ctx_heat::Cold,
        eff::ctx_resid::DRAM,
        eff::Row<eff::Effect::Bg>                       // Bg-effect row — incoherent
    >;
    BadCtx bad{};
    (void)bad;
    return 0;
}
