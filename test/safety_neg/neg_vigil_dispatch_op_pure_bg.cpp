// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I19 fixture — Vigil::dispatch_op_pure rejects Row<Effect::Bg>.
//
// IsPure<Row<Bg>> = false.  Vigil::dispatch_op is the FOREGROUND
// dispatch path; a Bg-context caller would mean the bg consumer
// thread is somehow producing entries through the Vigil API,
// breaking SPSC discipline (Vigil::assert_producer_thread_ would
// catch this at runtime in debug builds; the row fence catches it
// at compile time).
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Bg>>.

#include <crucible/Vigil.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry e{};
    crucible::TensorMeta m{};
    (void)vigil.dispatch_op_pure<eff::Row<eff::Effect::Bg>>(
        crucible::vouch(e), &m, 1);
    return 0;
}
