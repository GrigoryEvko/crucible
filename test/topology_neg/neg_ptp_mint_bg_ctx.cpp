// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Only an Init-row context mints a PTP handle.  A background context may
// record status and timestamps into a handle but may not create one.

#include <crucible/topology/Ptp.h>

int main() {
    crucible::cog::CogIdentity nic{};
    nic.uuid = crucible::cog::Uuid{0x129, 1};
    nic.kind = crucible::cog::CogKind::NicPort;
    auto fd = crucible::topology::admit_ptp_clock_fd(4).value();
    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto handle = crucible::topology::mint_ptp_handle(bg, nic, fd);
    (void)handle;
    return 0;
}
