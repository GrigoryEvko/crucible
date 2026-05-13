#include <crucible/cntp/_wip/Doca.h>

namespace crucible::cntp::_wip::doca {

std::string_view doca_error_name(DocaError error) noexcept {
    switch (error) {
        case DocaError::None: return "None";
        case DocaError::ZeroDpuCog: return "ZeroDpuCog";
        case DocaError::NonDpuCog: return "NonDpuCog";
        case DocaError::MissingDocaCapability:
            return "MissingDocaCapability";
        case DocaError::InvalidProgramId: return "InvalidProgramId";
        case DocaError::InvalidProgramImageBytes:
            return "InvalidProgramImageBytes";
        case DocaError::InvalidQueueDepth: return "InvalidQueueDepth";
        case DocaError::InvalidPayloadBytes: return "InvalidPayloadBytes";
        case DocaError::RuntimeUnavailable: return "RuntimeUnavailable";
        case DocaError::DeployDeferred: return "DeployDeferred";
        case DocaError::VendorBackendUnavailable:
            return "VendorBackendUnavailable";
        case DocaError::CommChannelUnavailable:
            return "CommChannelUnavailable";
        case DocaError::PayloadTooLarge: return "PayloadTooLarge";
        case DocaError::OutputBufferTooSmall: return "OutputBufferTooSmall";
        case DocaError::ProgramMismatch: return "ProgramMismatch";
        default: return "<unknown DocaError>";
    }
}

std::string_view doca_offload_kind_name(DocaOffloadKind kind) noexcept {
    switch (kind) {
        case DocaOffloadKind::SwimGossip: return "SwimGossip";
        case DocaOffloadKind::Scuttlebutt: return "Scuttlebutt";
        case DocaOffloadKind::Ktls: return "Ktls";
        case DocaOffloadKind::Compression: return "Compression";
        case DocaOffloadKind::Crypto: return "Crypto";
        case DocaOffloadKind::StorageEmulation: return "StorageEmulation";
        case DocaOffloadKind::FlowSteering: return "FlowSteering";
        default: return "<unknown DocaOffloadKind>";
    }
}

std::expected<OwnedDocaOffload, DocaError>
force_doca_backend_boundary(DeclaredDocaDeployPlan plan) noexcept {
    return deploy_doca_offload(plan);
}

}  // namespace crucible::cntp::_wip::doca
