// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A telemetry history is built at startup.  The mint admits a context
// whose row holds Init, and the background drain row does not.

#include <crucible/topology/Telemetry.h>

int main() {
    auto history =
        crucible::topology::mint_nic_telemetry_history<4>(::fixy::BgDrainCtx{::foundation::effects::testing::bg()});
    (void)history;
    return 0;
}
