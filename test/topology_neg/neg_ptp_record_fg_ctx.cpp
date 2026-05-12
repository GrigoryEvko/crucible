// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-129. Hot foreground contexts cannot mutate PTP
// clock status or timestamp state.

#include <crucible/topology/Ptp.h>

int main() {
    crucible::cog::CogIdentity nic{};
    nic.uuid = crucible::cog::Uuid{0x129, 2};
    nic.kind = crucible::cog::CogKind::NicPort;
    auto fd = crucible::topology::admit_ptp_clock_fd(5).value();
    auto handle = crucible::topology::mint_ptp_handle(
        crucible::effects::ColdInitCtx{}, nic, fd);
    handle.record_timestamp(
        crucible::effects::HotFgCtx{}, crucible::topology::PtpTimestampNs{7}, 1);
    return 0;
}
