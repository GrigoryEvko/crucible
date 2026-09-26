// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An attach plan is start-up work, so its mint takes a context that admits
// the initialization row.  A background drain context does not, and the
// gate refuses it.

#include <crucible/cntp/dataplane/TcEbpf.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    crucible::cntp::NicInterfaceName iface{};
    auto program = crucible::cntp::dataplane::mint_tc_program(
        bg, iface, crucible::cntp::dataplane::admit_xdp_ifindex(1).value(),
        crucible::cntp::dataplane::TcAttachPoint::Egress, crucible::cntp::dataplane::TcProgramKind::EgressMark);
    return program.value().ifindex.value() == 1u ? 0 : 1;
}
