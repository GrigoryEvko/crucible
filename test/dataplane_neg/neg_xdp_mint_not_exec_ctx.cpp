#include <crucible/cntp/dataplane/Xdp.h>

// A value that is not an execution context cannot stand in for one, so the
// attach plan cannot be minted from a bare struct.
struct NotAnExecCtx {};

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").value();
    auto ifindex = crucible::cntp::dataplane::admit_xdp_ifindex(7).value();
    auto program = crucible::cntp::dataplane::mint_xdp_program(NotAnExecCtx{}, iface, ifindex,
                                                               crucible::cntp::dataplane::XdpProgramKind::FlowFilter);
    return static_cast<int>(program.value().ifindex.value());
}
