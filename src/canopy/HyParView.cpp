#include <crucible/canopy/HyParView.h>

namespace crucible::canopy {

std::string_view hyparview_error_name(HyParViewError error) noexcept {
    switch (error) {
        case HyParViewError::ActiveViewFull:
            return "ActiveViewFull";
        case HyParViewError::DuplicatePeer:
            return "DuplicatePeer";
        case HyParViewError::EmptyActiveView:
            return "EmptyActiveView";
        case HyParViewError::InvalidConfig:
            return "InvalidConfig";
        case HyParViewError::PeerNotFound:
            return "PeerNotFound";
        case HyParViewError::ZeroUuid:
            return "ZeroUuid";
        default:
            return "Unknown";
    }
}

}  // namespace crucible::canopy
