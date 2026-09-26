// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A timestamp received from outside carries the External source.  It is
// not a timestamp of this clock, so it does not convert to one.

#include <crucible/canopy/Hlc.h>

void wants_hlc(crucible::canopy::HlcClockTimestamp);

int main() {
    const crucible::canopy::ExternalHlcTimestamp external = ::fixy::mint_tagged<::fixy::tags::source::External>(
        crucible::canopy::HlcTimestamp{.physical_ns = 1, .counter = 0});
    wants_hlc(external);
    return 0;
}
