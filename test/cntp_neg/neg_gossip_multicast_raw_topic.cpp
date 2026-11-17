// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A neighbor joins a topic that admit_gossip_topic declared.  A raw topic
// key does not convert to the declared topic, so the registration is
// refused.  The plan and the target are valid, so the topic is the one thing
// refused.

#include <crucible/cntp/GossipMulticast.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto const iface = crucible::cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");
    auto const ifindex = crucible::cntp::dataplane::admit_xdp_ifindex(7).expect("7 is an interface index");
    auto plan = crucible::cntp::mint_gossip_multicast_plan<4, 4>(init, iface, ifindex);
    crucible::cntp::GossipTopicKey const raw{.hash = 7};
    auto const target = crucible::cntp::gossip_neighbor_target(
        crucible::cog::CogIdentity{.uuid = crucible::cog::Uuid{1, 2}}, ifindex,
        crucible::cog::sriov::MacAddress::locally_administered(1), crucible::cntp::Ipv4AddressBe{0x0A000001u});
    return plan.register_neighbor(raw, target).is_ok() ? 0 : 1;
}
