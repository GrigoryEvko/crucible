// The compile-time checks of crucible/cntp/GossipMulticast.h.

#include <crucible/cntp/GossipMulticast.h>

namespace crucible::cntp {

static_assert(sizeof(DeclaredGossipTopic) == sizeof(GossipTopicKey));
static_assert(sizeof(DeclaredGossipMulticastPlan) == sizeof(GossipMulticastSpec));
static_assert(std::has_unique_object_representations_v<GossipTopicKey>);
static_assert(dataplane::BpfKey<GossipTopicKey>);
static_assert(dataplane::BpfScalar<GossipNeighborTarget>);

}  // namespace crucible::cntp
