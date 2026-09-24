#include <crucible/canopy/Plumtree.h>

namespace crucible::canopy {

std::string_view plumtree_error_name(PlumtreeError error) noexcept {
    switch (error) {
        case PlumtreeError::CapacityExceeded:
            return "CapacityExceeded";
        case PlumtreeError::DuplicatePeer:
            return "DuplicatePeer";
        case PlumtreeError::EmptyMessage:
            return "EmptyMessage";
        case PlumtreeError::InvalidConfig:
            return "InvalidConfig";
        case PlumtreeError::PeerNotFound:
            return "PeerNotFound";
        case PlumtreeError::TransientShapeInconsistency:
            return "TransientShapeInconsistency";
        case PlumtreeError::UnknownPeer:
            return "UnknownPeer";
        case PlumtreeError::ZeroUuid:
            return "ZeroUuid";
        default:
            return "Unknown";
    }
}

}  // namespace crucible::canopy
