// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// oob-62 fixture: ScuttlebuttRequestSet::count cannot be advanced from
// outside.
//
// This struct is the worse of the pair, because unlike the digest it has
// no well_formed() and no path anywhere validated it.  Incrementing the
// count past the bound used to be a bare `++set.count`, after which
// push() wrote outside `entries`.  reserve_next() is the only thing that
// moves the count now, and it refuses at the bound.

#include <crucible/canopy/Scuttlebutt.h>

int main() {
    namespace cc = crucible::canopy;
    cc::ScuttlebuttDiff<2, 2> diff{};  // capacity 4
    ++diff.requests.count;
    return 0;
}
