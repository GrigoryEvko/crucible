#include <crucible/cntp/OverlayMulticast.h>

namespace crucible::cntp {

std::string_view
overlay_multicast_error_name(OverlayMulticastError error) noexcept {
    switch (error) {
        case OverlayMulticastError::InvalidPeer:
            return "InvalidPeer";
        case OverlayMulticastError::DuplicatePeer:
            return "DuplicatePeer";
        case OverlayMulticastError::TooManyPeers:
            return "TooManyPeers";
        case OverlayMulticastError::InvalidStripeCount:
            return "InvalidStripeCount";
        case OverlayMulticastError::InvalidRecoveryThreshold:
            return "InvalidRecoveryThreshold";
        case OverlayMulticastError::InvalidFanout:
            return "InvalidFanout";
        case OverlayMulticastError::UnknownStripe:
            return "UnknownStripe";
        case OverlayMulticastError::FanoutExceeded:
            return "FanoutExceeded";
        case OverlayMulticastError::EmptyMessage:
            return "EmptyMessage";
        case OverlayMulticastError::MessageTooLarge:
            return "MessageTooLarge";
        default:
            return "Unknown";
    }
}

}  // namespace crucible::cntp
