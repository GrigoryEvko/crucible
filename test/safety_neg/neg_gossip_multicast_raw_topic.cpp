#include <crucible/cntp/GossipMulticast.h>

int main() {
    crucible::effects::ColdInitCtx init{};
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").value();
    auto ifindex = crucible::cntp::dataplane::admit_xdp_ifindex(7).value();
    auto plan = crucible::cntp::mint_gossip_multicast_plan<4, 4>(
        init, iface, ifindex);
    crucible::cntp::GossipTopicKey raw{.hash = 7};
    auto target = crucible::cntp::gossip_neighbor_target(
        crucible::cog::CogIdentity{.uuid = crucible::cog::Uuid{1, 2}},
        ifindex,
        {},
        0x0A00'0001U);
    return plan.register_neighbor(raw, target).has_value() ? 0 : 1;
}
