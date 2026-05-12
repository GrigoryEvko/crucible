// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-112 fixture #1: telemetry history storage is Init-owned.
// Background workers may record samples but cannot mint production storage.

#include <crucible/topology/Telemetry.h>

int main() {
    auto history = crucible::topology::mint_nic_telemetry_history<4>(
        crucible::effects::BgDrainCtx{});
    (void)history;
    return 0;
}
