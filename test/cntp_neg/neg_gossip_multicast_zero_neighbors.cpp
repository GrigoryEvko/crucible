// Negative compile test.  This file must fail to compile.
//
// A neighbor list holds at least one neighbor, so the gate of the plan
// refuses a plan of zero neighbors.  The context fits, so the shape is the
// one thing refused.

#include <crucible/cntp/GossipMulticast.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto const iface = crucible::cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");
    auto const ifindex = crucible::cntp::dataplane::admit_xdp_ifindex(7).expect("7 is an interface index");
    auto const plan = crucible::cntp::mint_gossip_multicast_plan<4, 0>(init, iface, ifindex);
    return plan.topic_count() == 0u ? 0 : 1;
}
