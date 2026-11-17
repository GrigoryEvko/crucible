// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The XDP program of a gossip plan comes from a plan that
// mint_gossip_multicast_plan declared.  A raw spec does not convert to the
// declared plan, so the call is refused.

#include <crucible/cntp/GossipMulticast.h>

int main() {
    crucible::cntp::GossipMulticastSpec const raw{};
    auto const program = crucible::cntp::gossip_multicast_xdp_program(raw);
    return program.value().required_features.test(crucible::cog::NicFeature::XdpNative) ? 0 : 1;
}
