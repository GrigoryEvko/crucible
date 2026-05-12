#include <crucible/cntp/GossipMulticast.h>

namespace crucible::cntp {

std::string_view
gossip_multicast_error_name(GossipMulticastError error) noexcept {
    switch (error) {
        case GossipMulticastError::EmptyTopic:
            return "EmptyTopic";
        case GossipMulticastError::InvalidTopicHash:
            return "InvalidTopicHash";
        case GossipMulticastError::InvalidDedupWindow:
            return "InvalidDedupWindow";
        case GossipMulticastError::InvalidPayloadLimit:
            return "InvalidPayloadLimit";
        case GossipMulticastError::InvalidPeer:
            return "InvalidPeer";
        case GossipMulticastError::DuplicateNeighbor:
            return "DuplicateNeighbor";
        case GossipMulticastError::TooManyNeighbors:
            return "TooManyNeighbors";
        case GossipMulticastError::TooManyTopics:
            return "TooManyTopics";
        case GossipMulticastError::UnknownTopic:
            return "UnknownTopic";
        case GossipMulticastError::PacketTooLarge:
            return "PacketTooLarge";
        case GossipMulticastError::EmptyPacket:
            return "EmptyPacket";
        case GossipMulticastError::IntegrityHashFailed:
            return "IntegrityHashFailed";
        default:
            return "Unknown";
    }
}

}  // namespace crucible::cntp
