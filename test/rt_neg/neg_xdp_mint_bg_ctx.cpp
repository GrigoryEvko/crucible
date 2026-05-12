#include <crucible/rt/Xdp.h>

int main() {
    crucible::effects::BgDrainCtx bg{};
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").value();
    auto ifindex = crucible::rt::admit_xdp_ifindex(7).value();
    auto program = crucible::rt::mint_xdp_program(
        bg, iface, ifindex, crucible::rt::XdpProgramKind::FlowFilter);
    return static_cast<int>(program.value().ifindex.value());
}
