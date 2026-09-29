// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Saturation row on the consumer side.  Pins that
// TraceRing::drain_pure rejects AllRow (the F* effect lattice top:
// every observable atom).  Pairs with the producer-side saturation
// fixture to witness the maximum-cardinality rejection input on
// both call directions.
//
// IsPure<AllRow> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<AllRow>.

#include <crucible/TraceRing.h>
#include <fixy/Aliases.h>

namespace eff = ::fixy;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry out[1];
    (void)ring.drain_pure<eff::AllRow>(out, 1);
    return 0;
}
