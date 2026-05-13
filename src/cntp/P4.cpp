#include <crucible/cntp/P4.h>

namespace crucible::cntp::p4 {

std::string_view p4_error_name(P4Error error) noexcept {
    switch (error) {
        case P4Error::None: return "None";
        case P4Error::ZeroSwitchCog: return "ZeroSwitchCog";
        case P4Error::NonSwitchCog: return "NonSwitchCog";
        case P4Error::MissingP4Capability: return "MissingP4Capability";
        case P4Error::InvalidProgramId: return "InvalidProgramId";
        case P4Error::InvalidSourceBytes: return "InvalidSourceBytes";
        case P4Error::InvalidTcamEntries: return "InvalidTcamEntries";
        case P4Error::InvalidStageCount: return "InvalidStageCount";
        case P4Error::InvalidRegisterWidthBits:
            return "InvalidRegisterWidthBits";
        case P4Error::TcamBudgetExceeded: return "TcamBudgetExceeded";
        case P4Error::CompilerUnavailable: return "CompilerUnavailable";
        case P4Error::CompileDeferred: return "CompileDeferred";
        case P4Error::DeploymentDeferred: return "DeploymentDeferred";
        case P4Error::VendorBackendUnavailable:
            return "VendorBackendUnavailable";
        default: return "<unknown P4Error>";
    }
}

std::string_view p4_program_kind_name(P4ProgramKind kind) noexcept {
    switch (kind) {
        case P4ProgramKind::IntTelemetry: return "IntTelemetry";
        case P4ProgramKind::SharpAssist: return "SharpAssist";
        case P4ProgramKind::ContentRoute: return "ContentRoute";
        case P4ProgramKind::FabricMulticast: return "FabricMulticast";
        case P4ProgramKind::FlowAcl: return "FlowAcl";
        case P4ProgramKind::LoadBalancer: return "LoadBalancer";
        default: return "<unknown P4ProgramKind>";
    }
}

std::expected<OwnedP4Deployment, P4Error>
force_p4_vendor_boundary(cog::CogIdentity sw,
                         cog::NvSwitchTargetCaps caps,
                         DeclaredP4Program program) noexcept {
    return deploy_p4_program(sw, caps, program);
}

}  // namespace crucible::cntp::p4
