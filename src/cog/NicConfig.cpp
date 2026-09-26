#include <crucible/cog/NicConfig.h>

namespace crucible::cog::nic {

std::expected<void, NicConfigError> apply_config(DeclaredNicConfig config) noexcept {
    auto valid = validate_nic_config(config.value());
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    if (!config.value().allow_privileged_apply) {
        return std::unexpected(NicConfigError::PrivilegedApplyDeferred);
    }
    return std::unexpected(NicConfigError::PrivilegedBackendUnavailable);
}

std::expected<void, NicConfigError> apply_ethtool(DeclaredEthtoolConfig config) noexcept {
    auto valid = validate_ethtool_config(config.value());
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return std::unexpected(NicConfigError::PrivilegedApplyDeferred);
}

std::expected<void, NicConfigError> apply_qdisc(DeclaredQdiscConfig config) noexcept {
    auto valid = validate_qdisc_config(config.value());
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return std::unexpected(NicConfigError::PrivilegedApplyDeferred);
}

std::expected<void, NicConfigError> apply_sysctl(DeclaredSysctlConfig config) noexcept {
    auto valid = validate_sysctl_config(config.value());
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return std::unexpected(NicConfigError::PrivilegedApplyDeferred);
}

std::expected<DeclaredNicConfig, NicConfigError> query_current(CogIdentity identity,
                                                               cntp::NicInterfaceName interface) noexcept {
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
