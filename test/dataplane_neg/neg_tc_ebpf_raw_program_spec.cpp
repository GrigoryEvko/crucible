// Negative compile test.  This file must fail to compile.
//
// The admission of a NIC takes a program that mint_tc_program declared.  A
// raw spec does not convert to the declared program, so the call is refused.

#include <crucible/cntp/dataplane/TcEbpf.h>

int main() {
    crucible::cog::CogIdentity const id{};
    crucible::cog::NicPortTargetCaps const caps{};
    crucible::cntp::dataplane::TcProgramSpec const raw{};
    return crucible::cntp::dataplane::tc_admit_nic(id, caps, raw).is_ok() ? 0 : 1;
}
