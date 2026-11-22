// Negative compile test.  This file must fail to compile.
//
// The admission of a NIC takes a program that mint_xdp_program declared.  A
// raw spec does not convert to the declared program, so the call is refused.

#include <crucible/cntp/dataplane/Xdp.h>

int main() {
    crucible::cog::CogIdentity id{};
    id.kind = crucible::cog::CogKind::NicPort;
    crucible::cog::NicPortTargetCaps const caps{};
    crucible::cntp::dataplane::XdpProgramSpec const raw{};
    return crucible::cntp::dataplane::xdp_admit_nic(id, caps, raw).is_ok() ? 0 : 1;
}
