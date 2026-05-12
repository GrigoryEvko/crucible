// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-112 fixture #3: telemetry history mutation is a background-row action.
// Hot foreground contexts can read snapshots but cannot publish new samples.

#include <crucible/topology/Telemetry.h>

int main() {
    auto history = crucible::topology::mint_nic_telemetry_history<2>(
        crucible::effects::ColdInitCtx{});
    crucible::topology::NicTelemetrySnapshot snapshot{};
    (void)history.record(crucible::effects::HotFgCtx{}, snapshot);
    return 0;
}
