// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The handle's constructor is private: mint_ptp_handle, gated on an
// Init-row context, is the only way to build one.

#include <crucible/topology/Ptp.h>

int main() {
    crucible::cog::CogIdentity nic{};
    nic.uuid = crucible::cog::Uuid{0x129, 4};
    nic.kind = crucible::cog::CogKind::NicPort;
    auto fd = crucible::topology::admit_ptp_clock_fd(6).value();
    crucible::topology::PtpHandle handle{nic, fd, crucible::topology::PtpStatus{}};
    (void)handle;
    return 0;
}
