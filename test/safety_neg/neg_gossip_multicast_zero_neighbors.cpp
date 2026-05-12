#include <crucible/cntp/GossipMulticast.h>

int main() {
    crucible::effects::ColdInitCtx init{};
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").value();
    auto ifindex = crucible::rt::admit_xdp_ifindex(7).value();
    auto plan = crucible::cntp::mint_gossip_multicast_plan<4, 0>(
        init, iface, ifindex);
    return static_cast<int>(plan.topic_count());
}
