// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-138 HLC provenance fixture #1: raw HlcTimestamp values cannot
// enter an API that requires source::Hlc provenance.

#include <crucible/canopy/Hlc.h>

void wants_hlc(crucible::canopy::HlcClockTimestamp);

int main() {
    crucible::canopy::HlcTimestamp raw{.physical_ns = 1, .counter = 0};
    wants_hlc(raw);
    return 0;
}
