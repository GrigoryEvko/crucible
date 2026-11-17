// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A value that is not an execution context cannot stand in for one, so the
// mint of an attach plan refuses a bare struct.

#include <crucible/cntp/dataplane/Xdp.h>

struct NotAnExecCtx {};

int main() {
    auto const iface = crucible::cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");
    auto const ifindex = crucible::cntp::dataplane::admit_xdp_ifindex(7).expect("7 is an interface index");
    auto const program = crucible::cntp::dataplane::mint_xdp_program(
        NotAnExecCtx{}, iface, ifindex, crucible::cntp::dataplane::XdpProgramKind::FlowFilter);
    return program.value().ifindex.value() == 7u ? 0 : 1;
}
