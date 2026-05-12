#include <crucible/cog/NicConfig.h>

namespace crucible::cog::nic {

std::string_view nic_config_error_name(NicConfigError error) noexcept {
    switch (error) {
        case NicConfigError::None:                         return "None";
        case NicConfigError::ZeroCog:                      return "ZeroCog";
        case NicConfigError::NonNicCog:                    return "NonNicCog";
        case NicConfigError::InvalidRingSize:              return "InvalidRingSize";
        case NicConfigError::InvalidQueueCount:            return "InvalidQueueCount";
        case NicConfigError::InvalidRssTableSize:          return "InvalidRssTableSize";
        case NicConfigError::InvalidSpeedMbps:             return "InvalidSpeedMbps";
        case NicConfigError::InvalidSysctlBytes:           return "InvalidSysctlBytes";
        case NicConfigError::InvalidSysctlPackets:         return "InvalidSysctlPackets";
        case NicConfigError::InvalidBusyPollUs:            return "InvalidBusyPollUs";
        case NicConfigError::InvalidTcpRtoMinUs:           return "InvalidTcpRtoMinUs";
        case NicConfigError::InvalidTcpMemoryTriple:       return "InvalidTcpMemoryTriple";
        case NicConfigError::InvalidInterfaceName:         return "InvalidInterfaceName";
        case NicConfigError::InterfaceMismatch:            return "InterfaceMismatch";
        case NicConfigError::PrivilegedApplyDeferred:      return "PrivilegedApplyDeferred";
        case NicConfigError::PrivilegedBackendUnavailable:
            return "PrivilegedBackendUnavailable";
        case NicConfigError::QueryDeferred:                return "QueryDeferred";
        default:                                           return "<unknown NicConfigError>";
    }
}

std::expected<void, NicConfigError>
apply_config(DeclaredNicConfig config) noexcept {
    auto valid = validate_nic_config(config.value());
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    if (!config.value().allow_privileged_apply) {
        return std::unexpected(NicConfigError::PrivilegedApplyDeferred);
    }
    return std::unexpected(NicConfigError::PrivilegedBackendUnavailable);
}

std::expected<void, NicConfigError>
apply_ethtool(DeclaredEthtoolConfig config) noexcept {
    auto valid = validate_ethtool_config(config.value());
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return std::unexpected(NicConfigError::PrivilegedApplyDeferred);
}

std::expected<void, NicConfigError>
apply_qdisc(DeclaredQdiscConfig config) noexcept {
    auto valid = validate_qdisc_config(config.value());
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return std::unexpected(NicConfigError::PrivilegedApplyDeferred);
}

std::expected<void, NicConfigError>
apply_sysctl(DeclaredSysctlConfig config) noexcept {
    auto valid = validate_sysctl_config(config.value());
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return std::unexpected(NicConfigError::PrivilegedApplyDeferred);
}

std::expected<DeclaredNicConfig, NicConfigError>
query_current(CogIdentity identity, cntp::NicInterfaceName interface) noexcept {
    static_cast<void>(interface);
    if (identity.uuid.is_zero()) {
        return std::unexpected(NicConfigError::ZeroCog);
    }
    if (identity.kind != CogKind::NicPort) {
        return std::unexpected(NicConfigError::NonNicCog);
    }
    return std::unexpected(NicConfigError::QueryDeferred);
}

}  // namespace crucible::cog::nic
