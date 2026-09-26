// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A telemetry history stores its indices in uint16_t.  A window too
// large for that carrier stops at a static assertion rather than wrap.

#include <crucible/topology/Telemetry.h>

int main() {
    auto history =
        crucible::topology::mint_nic_telemetry_history<65536>(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    (void)history;
    return 0;
}
