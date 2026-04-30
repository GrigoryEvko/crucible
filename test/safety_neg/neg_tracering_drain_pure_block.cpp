// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — pins TraceRing::drain_pure rejects a
// caller declaring Row<Effect::Block>.
//
// IsPure<Row<Block>> = false; rejection at the requires-clause.
// A blocking bg thread (e.g., one that holds a futex across the
// drain call) cannot silently invoke drain_pure — the row mismatch
// catches the contract violation before the SPSC tail advances.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Block>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry out[1];
    (void)ring.drain_pure<eff::Row<eff::Effect::Block>>(out, 1);
    return 0;
}
