// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A history has no lock and no atomic, so the foreground must not read
// it while the background records.  The foreground row owns neither Init
// nor Bg, so current_snapshot refuses the dispatch thread.

#include <crucible/topology/Telemetry.h>

[[maybe_unused]] static int read_from_foreground(::fixy::HotFgCtx const& fg) {
    auto history =
        crucible::topology::mint_nic_telemetry_history<2>(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    return history.current_snapshot(fg).has_value() ? 1 : 0;
}

int main() { return 0; }
