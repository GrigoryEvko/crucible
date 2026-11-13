// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Pins that TraceRing::drain_pure rejects a
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
#include <fixy/Aliases.h>

namespace eff = ::fixy;

int main() {
    crucible::TraceRing ring;
    ::fixy::FixedArray<crucible::TraceRing::Entry, 1> out{};
    (void)ring.drain_pure<eff::Row<eff::Effect::Init>>(out.view());
    return 0;
}
