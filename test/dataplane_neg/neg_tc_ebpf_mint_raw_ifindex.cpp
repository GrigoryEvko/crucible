// Negative compile test.  This file must fail to compile.
//
// An attach plan takes an admitted interface index.  A raw integer does not
// convert to the refined index, so the mint refuses it.  The context fits
// the gate, so the index is the one thing refused.

#include <crucible/cntp/dataplane/TcEbpf.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    crucible::cntp::NicInterfaceName const iface{};
    auto const program = crucible::cntp::dataplane::mint_tc_program(
        init, iface, std::uint32_t{11}, crucible::cntp::dataplane::TcAttachPoint::Egress,
        crucible::cntp::dataplane::TcProgramKind::EgressMark);
    return program.value().ifindex.value() == 11u ? 0 : 1;
}
