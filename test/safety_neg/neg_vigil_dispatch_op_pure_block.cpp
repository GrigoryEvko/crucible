// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I19 fixture — Vigil::dispatch_op_pure rejects Row<Effect::Block>.
//
// IsPure<Row<Block>> = false.  A blocking caller (futex, condvar,
// blocking syscall path) cannot run on the foreground hot dispatch
// path; the row mismatch catches the contract violation before any
// CrucibleContext mutation.  This is the most common bug class in
// production: an eager-fallback path that uses blocking primitives
// inadvertently routed through dispatch_op_pure.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Block>>.

#include <crucible/Vigil.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry e{};
    crucible::TensorMeta m{};
    (void)vigil.dispatch_op_pure<eff::Row<eff::Effect::Block>>(
        crucible::vouch(e), &m, 1);
    return 0;
}
