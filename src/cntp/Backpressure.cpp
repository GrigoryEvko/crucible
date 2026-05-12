#include <crucible/cntp/Backpressure.h>

namespace crucible::cntp {

std::string_view backpressure_error_name(BackpressureError error) noexcept {
    switch (error) {
        case BackpressureError::InvalidCreditBytes:      return "InvalidCreditBytes";
        case BackpressureError::InvalidConnectionLimit:  return "InvalidConnectionLimit";
        case BackpressureError::InvalidResourcePressure: return "InvalidResourcePressure";
        case BackpressureError::InvalidResourceLimit:    return "InvalidResourceLimit";
        case BackpressureError::CreditFlowNotStarted:    return "CreditFlowNotStarted";
        case BackpressureError::CreditExhausted:         return "CreditExhausted";
        case BackpressureError::CreditOverflow:          return "CreditOverflow";
        case BackpressureError::TooManyCreditFlows:      return "TooManyCreditFlows";
        case BackpressureError::TooManyResourceLimits:   return "TooManyResourceLimits";
        case BackpressureError::ConnectionLimitReached:  return "ConnectionLimitReached";
        case BackpressureError::ResourceLimitReached:    return "ResourceLimitReached";
        case BackpressureError::AdmissionRejected:       return "AdmissionRejected";
        default:                                        return "<unknown BackpressureError>";
    }
}

std::string_view
admission_decision_kind_name(AdmissionDecisionKind kind) noexcept {
    switch (kind) {
        case AdmissionDecisionKind::Accepted:         return "accepted";
        case AdmissionDecisionKind::RejectedBackoff:  return "rejected_backoff";
        case AdmissionDecisionKind::RejectedResource: return "rejected_resource";
        default:                                     return "unknown";
    }
}

}  // namespace crucible::cntp
