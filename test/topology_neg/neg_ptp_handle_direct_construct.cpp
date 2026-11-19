// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The handle's constructor is private: mint_ptp_handle, gated on an
// Init-row context, is the only way to build one.

#include <crucible/topology/Ptp.h>

int main() {
    crucible::cog::CogIdentity nic{};
    nic.uuid = crucible::cog::Uuid{0x129, 4};
    nic.kind = crucible::cog::CogKind::NicPort;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    crucible::topology::PtpHandle handle{nic, ::fixy::time::mint_empty_ptp_clock_reader(test),
                                         crucible::topology::PtpStatus{}};
    (void)handle;
    return 0;
}
