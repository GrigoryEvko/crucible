#include <crucible/topology/Health.h>

namespace crucible::topology {

std::string_view health_state_name(HealthState state) noexcept {
    switch (state) {
        case HealthState::Healthy:     return "Healthy";
        case HealthState::Suspect:     return "Suspect";
        case HealthState::Quarantined: return "Quarantined";
        case HealthState::Recovered:   return "Recovered";
        case HealthState::Permanent:   return "Permanent";
        default:                       return "<unknown HealthState>";
    }
}

std::string_view health_issue_name(HealthIssue issue) noexcept {
    switch (issue) {
        case HealthIssue::PhiSuspect:         return "PhiSuspect";
        case HealthIssue::PhiQuarantine:     return "PhiQuarantine";
        case HealthIssue::ThermalWarn:       return "ThermalWarn";
        case HealthIssue::ThermalCritical:   return "ThermalCritical";
        case HealthIssue::ClockDegraded:     return "ClockDegraded";
        case HealthIssue::CorrectedEccTrend: return "CorrectedEccTrend";
        case HealthIssue::UncorrectedEcc:    return "UncorrectedEcc";
        case HealthIssue::DropRateWarn:      return "DropRateWarn";
        case HealthIssue::DropRateCritical:  return "DropRateCritical";
        case HealthIssue::WearWarn:          return "WearWarn";
        case HealthIssue::WearCritical:      return "WearCritical";
        case HealthIssue::MissingSample:     return "MissingSample";
        default:                             return "<unknown HealthIssue>";
    }
}

}  // namespace crucible::topology
