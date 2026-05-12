// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-129. Raw integer fds cannot mint a PTP handle;
// fd admission must go through PtpClockFd.

#include <crucible/topology/Ptp.h>

int main() {
    crucible::cog::CogIdentity nic{};
    nic.uuid = crucible::cog::Uuid{0x129, 3};
    nic.kind = crucible::cog::CogKind::NicPort;
    auto handle = crucible::topology::mint_ptp_handle(
        crucible::effects::ColdInitCtx{}, nic, 3);
    (void)handle;
    return 0;
}
