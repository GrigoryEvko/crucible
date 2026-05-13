// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — multi-atom rejection on the consumer side.
// Pins TraceRing::drain_pure rejects Row<IO, Block> — same exact
// row Cipher::record_event REQUIRES.  Per the producer-side multi-
// atom fixture, this proves a row CORRECT for one row-typed entry
// point is REJECTED on a different one, and the constraint is not
// silently widened to "anything matching some other API's row".
//
// IsPure<Row<IO, Block>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::IO, Effect::Block>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry out[1];
    (void)ring.drain_pure<
        eff::Row<eff::Effect::IO, eff::Effect::Block>>(out, 1);
    return 0;
}
