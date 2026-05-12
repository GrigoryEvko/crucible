// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-129. PTP handles are minted only by Init
// contexts; background workers may publish admitted clock samples only.

#include <crucible/topology/Ptp.h>

int main() {
    crucible::cog::CogIdentity nic{};
    nic.uuid = crucible::cog::Uuid{0x129, 1};
    nic.kind = crucible::cog::CogKind::NicPort;
    auto fd = crucible::topology::admit_ptp_clock_fd(4).value();
    auto handle = crucible::topology::mint_ptp_handle(
        crucible::effects::BgDrainCtx{}, nic, fd);
    (void)handle;
    return 0;
}
