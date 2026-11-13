#include <crucible/cog/NicConfig.h>

namespace crucible::cog::nic {

::fixy::Result<::fixy::Unit, NicConfigError> apply_config(DeclaredNicConfig config) noexcept {
    CRUCIBLE_TRY(_, validate_nic_config(config.value()));
    if (!config.value().allow_privileged_apply) {
        return ::fixy::err(NicConfigError::PrivilegedApplyDeferred);
    }
    return ::fixy::err(NicConfigError::PrivilegedBackendUnavailable);
}

::fixy::Result<::fixy::Unit, NicConfigError> apply_ethtool(DeclaredEthtoolConfig config) noexcept {
    CRUCIBLE_TRY(_, validate_ethtool_config(config.value()));
    return ::fixy::err(NicConfigError::PrivilegedApplyDeferred);
}

::fixy::Result<::fixy::Unit, NicConfigError> apply_qdisc(DeclaredQdiscConfig config) noexcept {
    CRUCIBLE_TRY(_, validate_qdisc_config(config.value()));
    return ::fixy::err(NicConfigError::PrivilegedApplyDeferred);
}

::fixy::Result<::fixy::Unit, NicConfigError> apply_sysctl(DeclaredSysctlConfig config) noexcept {
    CRUCIBLE_TRY(_, validate_sysctl_config(config.value()));
    return ::fixy::err(NicConfigError::PrivilegedApplyDeferred);
}

::fixy::Result<DeclaredNicConfig, NicConfigError> query_current(CogIdentity identity,
                                                                cntp::NicInterfaceName interface) noexcept {
    static_cast<void>(interface);
    if (identity.uuid.is_zero()) {
        return ::fixy::err(NicConfigError::ZeroCog);
    }
    if (identity.kind != CogKind::NicPort) {
        return ::fixy::err(NicConfigError::NonNicCog);
    }
    return ::fixy::err(NicConfigError::QueryDeferred);
}

}  // namespace crucible::cog::nic
