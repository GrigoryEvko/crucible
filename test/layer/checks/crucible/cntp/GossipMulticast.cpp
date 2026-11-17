// The compile-time checks of crucible/cntp/GossipMulticast.h.

#include <crucible/cntp/GossipMulticast.h>

#include <type_traits>

namespace crucible::cntp {

static_assert(sizeof(DeclaredGossipTopic) == sizeof(GossipTopicKey));
static_assert(sizeof(DeclaredGossipMulticastPlan) == sizeof(GossipMulticastSpec));
static_assert(std::has_unique_object_representations_v<GossipTopicKey>);
static_assert(dataplane::BpfKey<GossipTopicKey>);
// A neighbor target has no padding that the compiler adds, so a map value
// that holds targets has no byte that no code writes.
static_assert(dataplane::BpfScalar<GossipNeighborTarget>);
static_assert(sizeof(GossipNeighborTarget) == 32);
static_assert(std::has_unique_object_representations_v<GossipNeighborTarget>);
static_assert(dataplane::BpfMapElement<GossipNeighborList<2>>);
static_assert(std::has_unique_object_representations_v<GossipNeighborList<2>>);
static_assert(GossipMulticastShape<1, 1>);
static_assert(!GossipMulticastShape<0, 1>);
static_assert(!GossipMulticastShape<1, 0>);
static_assert(CtxFitsGossipMulticastMint<::fixy::ColdInitCtx, 4, 4>);
static_assert(!CtxFitsGossipMulticastMint<::fixy::BgDrainCtx, 4, 4>);
static_assert(admit_gossip_topic_hash(0).is_err());
static_assert(GossipNeighborTarget{}.ifindex().value() == 1u);

}  // namespace crucible::cntp
