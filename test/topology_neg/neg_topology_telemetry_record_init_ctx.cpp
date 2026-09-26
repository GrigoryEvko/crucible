// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A record writes the history, and the background owns every write.  The
// startup row holds no Bg, so record refuses it.  The snapshot comes in
// as a parameter, because only the mint builds one.

#include <crucible/topology/Telemetry.h>

[[maybe_unused]] static int record_from_startup(crucible::topology::NicTelemetrySnapshot const& snapshot) {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto history = crucible::topology::mint_nic_telemetry_history<2>(init);
    return static_cast<int>(history.record(init, snapshot));
}

int main() { return 0; }
