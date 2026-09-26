// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A record writes the history, and the background owns every write.  The
// foreground row is empty, so record refuses the dispatch thread.  The
// foreground context and the snapshot come in as parameters, because
// neither is built here.

#include <crucible/topology/Telemetry.h>

[[maybe_unused]] static int record_from_foreground(::fixy::HotFgCtx const& fg,
                                                   crucible::topology::NicTelemetrySnapshot const& snapshot) {
    auto history =
        crucible::topology::mint_nic_telemetry_history<2>(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    return static_cast<int>(history.record(fg, snapshot));
}

int main() { return 0; }
