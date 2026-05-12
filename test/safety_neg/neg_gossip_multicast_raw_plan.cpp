#include <crucible/cntp/GossipMulticast.h>

int main() {
    crucible::cntp::GossipMulticastSpec raw{};
    auto program = crucible::cntp::gossip_multicast_xdp_program(raw);
    return static_cast<int>(
        program.value().required_features.test(
            crucible::cog::NicFeature::XdpNative));
}
