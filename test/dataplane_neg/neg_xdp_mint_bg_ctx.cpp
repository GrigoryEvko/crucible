#include <crucible/cntp/dataplane/Xdp.h>

int main() {
    crucible::effects::BgDrainCtx bg{};
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").value();
    auto ifindex = crucible::cntp::dataplane::admit_xdp_ifindex(7).value();
    auto program = crucible::cntp::dataplane::mint_xdp_program(
        bg, iface, ifindex, crucible::cntp::dataplane::XdpProgramKind::FlowFilter);
    return static_cast<int>(program.value().ifindex.value());
}
