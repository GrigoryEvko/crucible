#include <crucible/topology/AsymmetricFailure.h>

namespace crucible::topology {

std::string_view failure_class_name(FailureClass cls) noexcept {
    switch (cls) {
        case FailureClass::BidiOk:       return "BidiOk";
        case FailureClass::TxBroken:     return "TxBroken";
        case FailureClass::RxBroken:     return "RxBroken";
        case FailureClass::BidiFailed:   return "BidiFailed";
        case FailureClass::Inconclusive: return "Inconclusive";
        default:                         return "<unknown FailureClass>";
    }
}

std::string_view failure_signal_name(FailureSignal signal) noexcept {
    switch (signal) {
        case FailureSignal::LocalOutboundFailed:       return "LocalOutboundFailed";
        case FailureSignal::LocalInboundFailed:        return "LocalInboundFailed";
        case FailureSignal::WitnessReachable:          return "WitnessReachable";
        case FailureSignal::WitnessUnreachable:        return "WitnessUnreachable";
        case FailureSignal::WitnessMajorityReachable:  return "WitnessMajorityReachable";
        case FailureSignal::WitnessMajorityUnreachable:return "WitnessMajorityUnreachable";
        case FailureSignal::InsufficientLocalSamples:  return "InsufficientLocalSamples";
        case FailureSignal::InsufficientWitnesses:     return "InsufficientWitnesses";
        default:                                       return "<unknown FailureSignal>";
    }
}

}  // namespace crucible::topology
