// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-138 HLC provenance fixture #2: externally received timestamps
// are not HLC-minted timestamps until an admission site retags them.

#include <crucible/canopy/Hlc.h>

void wants_hlc(crucible::canopy::HlcClockTimestamp);

int main() {
    crucible::canopy::ExternalHlcTimestamp external{
        crucible::canopy::HlcTimestamp{.physical_ns = 1, .counter = 0}};
    wants_hlc(external);
    return 0;
}
