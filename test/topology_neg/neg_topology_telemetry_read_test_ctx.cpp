// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A history is read only by a context that owns it, and the test runner
// row owns neither Init nor Bg.  A context that can reach the history
// through no owner therefore cannot read it.

#include <crucible/topology/Telemetry.h>

int main() {
    auto history =
        crucible::topology::mint_nic_telemetry_history<2>(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    ::fixy::TestRunnerCtx runner{::foundation::effects::testing::test()};
    return history.current_snapshot(runner).has_value() ? 1 : 0;
}
