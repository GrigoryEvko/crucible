#include <crucible/cntp/dataplane/TcEbpf.h>

int main() {
    crucible::effects::BgDrainCtx bg{};
    crucible::cntp::NicInterfaceName iface{};
    auto program = crucible::cntp::dataplane::mint_tc_program(
        bg, iface, crucible::cntp::dataplane::TcIfIndex{std::uint32_t{1}},
        crucible::cntp::dataplane::TcAttachPoint::Egress,
        crucible::cntp::dataplane::TcProgramKind::EgressMark);
    return program.value().ifindex.value() == 1u ? 0 : 1;
}
