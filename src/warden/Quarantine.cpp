#include <crucible/warden/Quarantine.h>

namespace crucible::warden {

std::string_view quarantine_state_name(QuarantineState state) noexcept {
    switch (state) {
        case QuarantineState::Healthy:     return "Healthy";
        case QuarantineState::Suspect:     return "Suspect";
        case QuarantineState::Quarantined: return "Quarantined";
        case QuarantineState::Recovered:   return "Recovered";
        case QuarantineState::Permanent:   return "Permanent";
        default:                           return "<unknown QuarantineState>";
    }
}

std::string_view quarantine_signal_name(QuarantineSignal signal) noexcept {
    switch (signal) {
        case QuarantineSignal::HealthSuspect:              return "HealthSuspect";
        case QuarantineSignal::HealthQuarantine:           return "HealthQuarantine";
        case QuarantineSignal::CriticalHealthIssue:        return "CriticalHealthIssue";
        case QuarantineSignal::AsymmetricSuspect:          return "AsymmetricSuspect";
        case QuarantineSignal::AsymmetricDead:             return "AsymmetricDead";
        case QuarantineSignal::RecoveryProbePassed:        return "RecoveryProbePassed";
        case QuarantineSignal::RecoveryProbeFailed:        return "RecoveryProbeFailed";
        case QuarantineSignal::RecoveryThresholdMet:       return "RecoveryThresholdMet";
        case QuarantineSignal::PermanentTimerElapsed:      return "PermanentTimerElapsed";
        case QuarantineSignal::PermanentRequiresOperator:  return "PermanentRequiresOperator";
        case QuarantineSignal::OperatorOverride:           return "OperatorOverride";
        default:                                           return "<unknown QuarantineSignal>";
    }
}

}  // namespace crucible::warden
