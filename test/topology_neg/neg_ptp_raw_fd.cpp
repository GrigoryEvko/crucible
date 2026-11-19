// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A PTP handle takes the reader of the clock of its NIC.  A raw int does
// not convert to a reader, so the number of a descriptor cannot name the
// clock of a handle.

#include <crucible/topology/Ptp.h>

int main() {
    crucible::cog::CogIdentity nic{};
    nic.uuid = crucible::cog::Uuid{0x129, 3};
    nic.kind = crucible::cog::CogKind::NicPort;
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto handle = crucible::topology::mint_ptp_handle(init, nic, 3);
    (void)handle;
    return 0;
}
