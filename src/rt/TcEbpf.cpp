#include <crucible/rt/TcEbpf.h>

namespace crucible::rt {

std::string_view tc_action_name(TcAction action) noexcept {
    switch (action) {
        case TcAction::Ok:         return "Ok";
        case TcAction::Shot:       return "Shot";
        case TcAction::Stolen:     return "Stolen";
        case TcAction::Pipe:       return "Pipe";
        case TcAction::Reclassify: return "Reclassify";
        case TcAction::Trap:       return "Trap";
        default:                   return "Unknown";
    }
}

std::string_view tc_attach_point_name(TcAttachPoint point) noexcept {
    switch (point) {
        case TcAttachPoint::Ingress: return "Ingress";
        case TcAttachPoint::Egress:  return "Egress";
        default:                     return "Unknown";
    }
}

std::string_view tc_program_kind_name(TcProgramKind kind) noexcept {
    switch (kind) {
        case TcProgramKind::EgressMark:      return "EgressMark";
        case TcProgramKind::PacingGate:      return "PacingGate";
        case TcProgramKind::QuarantineDrop:  return "QuarantineDrop";
        case TcProgramKind::FlowTelemetry:   return "FlowTelemetry";
        case TcProgramKind::IngressClassify: return "IngressClassify";
        default:                             return "Unknown";
    }
}

std::string_view tc_error_name(TcError error) noexcept {
    switch (error) {
        case TcError::InvalidIfIndex:            return "InvalidIfIndex";
        case TcError::InvalidDscp:               return "InvalidDscp";
        case TcError::InvalidClassId:            return "InvalidClassId";
        case TcError::InvalidFlowPriority:       return "InvalidFlowPriority";
        case TcError::WrongCogKind:              return "WrongCogKind";
        case TcError::MissingTcEbpf:             return "MissingTcEbpf";
        case TcError::PrivilegedAttachDeferred:  return "PrivilegedAttachDeferred";
        default:                                 return "Unknown";
    }
}

std::expected<void, TcError>
attach_tc_program(DeclaredTcProgram program) noexcept {
    static_cast<void>(program);
    return std::unexpected(TcError::PrivilegedAttachDeferred);
}

}  // namespace crucible::rt
