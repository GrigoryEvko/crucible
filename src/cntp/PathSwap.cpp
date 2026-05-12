#include <crucible/cntp/PathSwap.h>

namespace crucible::cntp {

std::string_view swap_state_name(SwapState state) noexcept {
    switch (state) {
        case SwapState::Stable:          return "Stable";
        case SwapState::Draining:        return "Draining";
        case SwapState::BidirReceive:    return "BidirReceive";
        case SwapState::NewPathFlushing: return "NewPathFlushing";
        case SwapState::Complete:        return "Complete";
        case SwapState::Failed:          return "Failed";
        default:                         return "<unknown SwapState>";
    }
}

std::string_view swap_error_name(SwapError error) noexcept {
    switch (error) {
        case SwapError::InvalidPathId:     return "InvalidPathId";
        case SwapError::SamePath:          return "SamePath";
        case SwapError::DeadlineOverflow:  return "DeadlineOverflow";
        case SwapError::Timeout:           return "Timeout";
        case SwapError::InvalidTransition: return "InvalidTransition";
        default:                           return "<unknown SwapError>";
    }
}

}  // namespace crucible::cntp
