#include <crucible/rt/TcEbpf.h>

int main() {
    crucible::effects::BgDrainCtx bg{};
    crucible::cntp::NicInterfaceName iface{};
    auto program = crucible::rt::mint_tc_program(
        bg, iface, crucible::rt::TcIfIndex{std::uint32_t{1}},
        crucible::rt::TcAttachPoint::Egress,
        crucible::rt::TcProgramKind::EgressMark);
    return program.value().ifindex.value() == 1u ? 0 : 1;
}
