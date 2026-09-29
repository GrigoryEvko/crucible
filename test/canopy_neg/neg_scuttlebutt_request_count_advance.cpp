// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ScuttlebuttRequestSet::count cannot be advanced from outside.  The
// request set has no well_formed(), so the type is its only bound.
// reserve_next() is the only thing that moves the count, and it refuses
// at the capacity.

#include <crucible/canopy/Scuttlebutt.h>

int main() {
    namespace cc = crucible::canopy;
    cc::ScuttlebuttDiff<2, 2> diff{};  // capacity 4
    ++diff.requests.count;
    return 0;
}
