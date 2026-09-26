#include <crucible/cntp/dataplane/TcEbpf.h>

int main() {
    crucible::effects::BgDrainCtx bg{::crucible::effects::testing::bg()};
    crucible::cntp::NicInterfaceName iface{};
    auto program = crucible::cntp::dataplane::mint_tc_program(
        bg, iface, crucible::cntp::dataplane::admit_xdp_ifindex(1).value(),
        crucible::cntp::dataplane::TcAttachPoint::Egress, crucible::cntp::dataplane::TcProgramKind::EgressMark);
    return program.value().ifindex.value() == 1u ? 0 : 1;
}
