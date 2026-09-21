// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I19 fixture — saturation-row rejection.  Pins
// Vigil::dispatch_op_pure rejects AllRow (the F* effect lattice top:
// every observable atom).  Maximum-cardinality rejection witness;
// catches a regression that special-cased "below 6 atoms".
//
// IsPure<AllRow> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<AllRow>.

#include <crucible/Vigil.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_FxAliases.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry e{};
    crucible::TensorMeta m{};
    (void)vigil.dispatch_op_pure<eff::AllRow>(crucible::TraceRing::ValidatedEntryPtr{&e}, &m, 1);
    return 0;
}
