#include <crucible/cntp/GossipMulticast.h>

int main() {
    crucible::effects::BgDrainCtx bg{};
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").value();
    auto ifindex = crucible::cntp::dataplane::admit_xdp_ifindex(7).value();
    auto plan = crucible::cntp::mint_gossip_multicast_plan<4, 4>(
        bg, iface, ifindex);
    return static_cast<int>(plan.topic_count());
}
