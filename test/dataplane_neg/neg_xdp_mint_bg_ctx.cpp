// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An attach plan is start-up work, so its mint takes a context that owns the
// initialization capability.  A background drain context does not own it,
// and the gate refuses it.

#include <crucible/cntp/dataplane/Xdp.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto const iface = crucible::cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");
    auto const ifindex = crucible::cntp::dataplane::admit_xdp_ifindex(7).expect("7 is an interface index");
    auto const program = crucible::cntp::dataplane::mint_xdp_program(
        bg, iface, ifindex, crucible::cntp::dataplane::XdpProgramKind::FlowFilter);
    return program.value().ifindex.value() == 7u ? 0 : 1;
}
