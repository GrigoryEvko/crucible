// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-112 audit fixture: telemetry history stores indices in uint16_t.
// Oversized compile-time windows must fail instead of silently wrapping.

#include <crucible/topology/Telemetry.h>

int main() {
    auto history = crucible::topology::mint_nic_telemetry_history<65'536>(
        crucible::effects::ColdInitCtx{});
    (void)history;
    return 0;
}
