// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A gossip plan mints an XDP program, which is start-up work.  A background
// drain context does not own the initialization capability, so the gate of
// the plan refuses it.

#include <crucible/cntp/GossipMulticast.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto const iface = crucible::cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");
    auto const ifindex = crucible::cntp::dataplane::admit_xdp_ifindex(7).expect("7 is an interface index");
    auto const plan = crucible::cntp::mint_gossip_multicast_plan<4, 4>(bg, iface, ifindex);
    return plan.topic_count() == 0u ? 0 : 1;
}
