#include <crucible/cntp/Tcam.h>

namespace crucible::cntp::tcam {

std::string_view tcam_error_name(TcamError error) noexcept {
    switch (error) {
        case TcamError::None: return "None";
        case TcamError::ZeroTargetCog: return "ZeroTargetCog";
        case TcamError::WrongTargetKind: return "WrongTargetKind";
        case TcamError::MissingTcamCapability: return "MissingTcamCapability";
        case TcamError::InvalidRuleId: return "InvalidRuleId";
        case TcamError::InvalidEntryCount: return "InvalidEntryCount";
        case TcamError::InvalidMatchParameter: return "InvalidMatchParameter";
        case TcamError::InvalidActionParameter:
            return "InvalidActionParameter";
        case TcamError::CapacityExceeded: return "CapacityExceeded";
        case TcamError::TableFull: return "TableFull";
        case TcamError::InvalidRuleHandle: return "InvalidRuleHandle";
        case TcamError::CounterOverflow: return "CounterOverflow";
        case TcamError::VendorBackendUnavailable:
            return "VendorBackendUnavailable";
        default: return "<unknown TcamError>";
    }
}

std::string_view tcam_target_kind_name(TcamTargetKind kind) noexcept {
    switch (kind) {
        case TcamTargetKind::NicPort: return "NicPort";
        case TcamTargetKind::Switch: return "Switch";
        default: return "<unknown TcamTargetKind>";
    }
}

std::string_view flow_action_name(FlowAction action) noexcept {
    switch (action) {
        case FlowAction::Drop: return "Drop";
        case FlowAction::Pass: return "Pass";
        case FlowAction::Redirect: return "Redirect";
        case FlowAction::MarkDscp: return "MarkDscp";
        case FlowAction::Count: return "Count";
        case FlowAction::Mirror: return "Mirror";
        default: return "<unknown FlowAction>";
    }
}

std::expected<void, TcamError>
force_tcam_backend_boundary(DeclaredTcamTable table,
                            DeclaredTcamFlowRule rule) noexcept {
    auto valid = validate_tcam_rule(rule.value());
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    if (!table.value().backend_ready) {
        return std::unexpected(TcamError::VendorBackendUnavailable);
    }
    // fixy-A5-025: success fallthrough — both predicates hold, the
    // boundary work (currently a no-op until the vendor backend ships)
    // is "done".  Pre-fix this returned VendorBackendUnavailable on the
    // happy path, indistinguishable from the genuine not-ready case.
    return {};
}

}  // namespace crucible::cntp::tcam
