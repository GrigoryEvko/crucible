#include <crucible/cog/SrIov.h>

namespace crucible::cog::sriov {

std::string_view sriov_error_name(SrIovError error) noexcept {
    switch (error) {
        case SrIovError::None:                       return "None";
        case SrIovError::ZeroCog:                    return "ZeroCog";
        case SrIovError::NonNicCog:                  return "NonNicCog";
        case SrIovError::MissingSrIovCapability:     return "MissingSrIovCapability";
        case SrIovError::InvalidInterfaceName:       return "InvalidInterfaceName";
        case SrIovError::InvalidVfCount:             return "InvalidVfCount";
        case SrIovError::InvalidVfIndex:             return "InvalidVfIndex";
        case SrIovError::InvalidMac:                 return "InvalidMac";
        case SrIovError::InvalidVlan:                return "InvalidVlan";
        case SrIovError::InvalidRateLimit:           return "InvalidRateLimit";
        case SrIovError::InvalidResourceLimit:       return "InvalidResourceLimit";
        case SrIovError::VfIndexOutOfRange:          return "VfIndexOutOfRange";
        case SrIovError::InsufficientHandleCapacity:
            return "InsufficientHandleCapacity";
        case SrIovError::PrivilegedApplyDeferred:    return "PrivilegedApplyDeferred";
        case SrIovError::PrivilegedBackendUnavailable:
            return "PrivilegedBackendUnavailable";
        case SrIovError::QueryDeferred:              return "QueryDeferred";
        default:                                     return "<unknown SrIovError>";
    }
}

std::expected<std::span<VfHandle>, SrIovError>
SrIovManager::enable(DeclaredSrIovPlan plan,
                     std::span<VfHandle> out) noexcept {
    auto handles = materialize_vf_handles(plan, out);
    if (!handles.has_value()) {
        return std::unexpected(handles.error());
    }
    if (!plan.value().allow_privileged_apply) {
        return std::unexpected(SrIovError::PrivilegedApplyDeferred);
    }
    return std::unexpected(SrIovError::PrivilegedBackendUnavailable);
}

std::expected<void, SrIovError>
SrIovManager::configure_vf(VfHandle handle, DeclaredVfConfig config) noexcept {
    if (handle.identity.uuid.is_zero() || handle.parent_uuid.is_zero()) {
        return std::unexpected(SrIovError::ZeroCog);
    }
    auto valid = validate_vf_config(config.value());
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return std::unexpected(SrIovError::PrivilegedApplyDeferred);
}

std::expected<void, SrIovError>
SrIovManager::disable(DeclaredSrIovPlan plan) noexcept {
    if (!plan.value().allow_privileged_apply) {
        return std::unexpected(SrIovError::PrivilegedApplyDeferred);
    }
    return std::unexpected(SrIovError::PrivilegedBackendUnavailable);
}

std::expected<std::span<VfHandle>, SrIovError>
enable(DeclaredSrIovPlan plan, std::span<VfHandle> out) noexcept {
    SrIovManager manager{};
    return manager.enable(plan, out);
}

std::expected<void, SrIovError>
configure_vf(VfHandle handle, DeclaredVfConfig config) noexcept {
    SrIovManager manager{};
    return manager.configure_vf(handle, config);
}

std::expected<void, SrIovError>
disable(DeclaredSrIovPlan plan) noexcept {
    SrIovManager manager{};
    return manager.disable(plan);
}

std::expected<DeclaredSrIovPlan, SrIovError>
query_current(CogIdentity physical, cntp::NicInterfaceName interface) noexcept {
    if (physical.uuid.is_zero()) {
        return std::unexpected(SrIovError::ZeroCog);
    }
    if (physical.kind != CogKind::NicPort) {
        return std::unexpected(SrIovError::NonNicCog);
    }
    if (!interface_name_present(interface)) {
        return std::unexpected(SrIovError::InvalidInterfaceName);
    }
    return std::unexpected(SrIovError::QueryDeferred);
}

}  // namespace crucible::cog::sriov
