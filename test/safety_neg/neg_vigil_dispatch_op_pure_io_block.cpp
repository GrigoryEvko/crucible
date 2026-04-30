// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I19 fixture — multi-atom rejection.  Pins
// Vigil::dispatch_op_pure rejects Row<IO, Block> — exactly the row
// Cipher::record_event REQUIRES (per FOUND-I09).  Pins compositional
// non-triviality on the Vigil leg: a row CORRECT for one row-typed
// entry point is REJECTED at another.  Catches the regression where
// IsPure is silently widened to "anything matching the record_event
// row".
//
// IsPure<Row<IO, Block>> = Subrow<Row<IO, Block>, Row<>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::IO, Effect::Block>>.

#include <crucible/Vigil.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry e{};
    crucible::TensorMeta m{};
    (void)vigil.dispatch_op_pure<
        eff::Row<eff::Effect::IO, eff::Effect::Block>>(
            crucible::vouch(e), &m, 1);
    return 0;
}
