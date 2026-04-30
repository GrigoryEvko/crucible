// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — pins TraceRing::drain_pure rejects a
// caller declaring Row<Effect::Init>.
//
// IsPure<Row<Init>> = false.  Init is the one-shot startup phase
// before bg pumping has begun; calling drain_pure from Init
// would mean the consumer is being driven before the producer
// has been wired up.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Init>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry out[1];
    (void)ring.drain_pure<eff::Row<eff::Effect::Init>>(out, 1);
    return 0;
}
