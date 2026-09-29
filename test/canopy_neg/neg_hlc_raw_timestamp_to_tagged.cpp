// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A raw HlcTimestamp carries no source.  It does not convert to a
// timestamp of this clock, so it cannot enter an API that demands one.

#include <crucible/canopy/Hlc.h>

void wants_hlc(crucible::canopy::HlcClockTimestamp);

int main() {
    crucible::canopy::HlcTimestamp raw{.physical_ns = 1, .counter = 0};
    wants_hlc(raw);
    return 0;
}
