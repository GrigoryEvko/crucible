#include <crucible/canopy/Lifeguard.h>

namespace crucible::canopy {

std::string_view lifeguard_error_name(LifeguardError error) noexcept {
    switch (error) {
        case LifeguardError::CapacityExceeded:
            return "CapacityExceeded";
        case LifeguardError::DuplicatePeer:
            return "DuplicatePeer";
        case LifeguardError::InvalidConfig:
            return "InvalidConfig";
        case LifeguardError::PeerNotFound:
            return "PeerNotFound";
        case LifeguardError::ZeroUuid:
            return "ZeroUuid";
        default:
            return "Unknown";
    }
}

}  // namespace crucible::canopy
