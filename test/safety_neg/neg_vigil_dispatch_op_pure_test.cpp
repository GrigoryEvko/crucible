// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I19 fixture — Vigil::dispatch_op_pure rejects Row<Effect::Test>.
//
// IsPure<Row<Test>> = false.  Test context (poison-byte injection,
// invariant probes, mock fixtures) cannot leak into production
// dispatch_op_pure call sites — and conversely, production
// dispatch_op_pure cannot be invoked from Test-tagged contexts.
// The row fence enforces the cross-tier separation at compile
// time.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Test>>.

#include <crucible/Vigil.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry e{};
    crucible::TensorMeta m{};
    (void)vigil.dispatch_op_pure<eff::Row<eff::Effect::Test>>(
        crucible::vouch(e), &m, 1);
    return 0;
}
