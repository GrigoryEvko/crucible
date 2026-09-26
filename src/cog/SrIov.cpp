#include <crucible/cog/SrIov.h>

// The SrIovManager methods these free-function forwarders dispatch to are
// declared deprecated.  The suppression is scoped to this file so that callers
// outside it still observe the warning at their own call sites.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace crucible::cog::sriov {

std::expected<std::span<VfHandle>, SrIovError> SrIovManager::enable(DeclaredSrIovPlan plan,
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

// The configuration needs no check: each of its fields is refined.  An
// empty handle slot names no function, so it is refused here.
std::expected<void, SrIovError> SrIovManager::configure_vf(VfHandle handle, DeclaredVfConfig config) noexcept {
    static_cast<void>(config);
    if (handle.identity().uuid.is_zero() || handle.parent_uuid().is_zero()) {
        return std::unexpected(SrIovError::ZeroCog);
    }
    return std::unexpected(SrIovError::PrivilegedApplyDeferred);
}

std::expected<void, SrIovError> SrIovManager::disable(DeclaredSrIovPlan plan) noexcept {
    if (!plan.value().allow_privileged_apply) {
        return std::unexpected(SrIovError::PrivilegedApplyDeferred);
    }
    return std::unexpected(SrIovError::PrivilegedBackendUnavailable);
}

std::expected<std::span<VfHandle>, SrIovError> enable(DeclaredSrIovPlan plan, std::span<VfHandle> out) noexcept {
    SrIovManager manager{};
    return manager.enable(plan, out);
}

std::expected<void, SrIovError> configure_vf(VfHandle handle, DeclaredVfConfig config) noexcept {
    SrIovManager manager{};
    return manager.configure_vf(handle, config);
}

std::expected<void, SrIovError> disable(DeclaredSrIovPlan plan) noexcept {
    SrIovManager manager{};
    return manager.disable(plan);
}

std::expected<DeclaredSrIovPlan, SrIovError> query_current(CogIdentity physical,
                                                           cntp::NicInterfaceName interface) noexcept {
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

#pragma GCC diagnostic pop
