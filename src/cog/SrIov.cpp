#include <crucible/cog/SrIov.h>

// The SrIovManager methods these free-function forwarders dispatch to are
// declared deprecated.  The suppression is scoped to this file so that callers
// outside it still observe the warning at their own call sites.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace crucible::cog::sriov {

::fixy::Result<::fixy::View<VfHandle>, SrIovError> SrIovManager::enable(DeclaredSrIovPlan plan,
                                                                        ::fixy::View<VfHandle> out) noexcept {
    CRUCIBLE_TRY(_, materialize_vf_handles(plan, out));
    if (!plan.value().allow_privileged_apply) {
        return ::fixy::err(SrIovError::PrivilegedApplyDeferred);
    }
    return ::fixy::err(SrIovError::PrivilegedBackendUnavailable);
}

// The configuration needs no check: each of its fields is refined.  An
// empty handle slot names no function, so it is refused here.
::fixy::Result<::fixy::Unit, SrIovError> SrIovManager::configure_vf(VfHandle handle, DeclaredVfConfig config) noexcept {
    static_cast<void>(config);
    if (handle.identity().uuid.is_zero() || handle.parent_uuid().is_zero()) {
        return ::fixy::err(SrIovError::ZeroCog);
    }
    return ::fixy::err(SrIovError::PrivilegedApplyDeferred);
}

::fixy::Result<::fixy::Unit, SrIovError> SrIovManager::disable(DeclaredSrIovPlan plan) noexcept {
    if (!plan.value().allow_privileged_apply) {
        return ::fixy::err(SrIovError::PrivilegedApplyDeferred);
    }
    return ::fixy::err(SrIovError::PrivilegedBackendUnavailable);
}

::fixy::Result<::fixy::View<VfHandle>, SrIovError> enable(DeclaredSrIovPlan plan, ::fixy::View<VfHandle> out) noexcept {
    SrIovManager manager{};
    return manager.enable(plan, out);
}

::fixy::Result<::fixy::Unit, SrIovError> configure_vf(VfHandle handle, DeclaredVfConfig config) noexcept {
    SrIovManager manager{};
    return manager.configure_vf(handle, config);
}

::fixy::Result<::fixy::Unit, SrIovError> disable(DeclaredSrIovPlan plan) noexcept {
    SrIovManager manager{};
    return manager.disable(plan);
}

::fixy::Result<DeclaredSrIovPlan, SrIovError> query_current(CogIdentity physical,
                                                            cntp::NicInterfaceName interface) noexcept {
    if (physical.uuid.is_zero()) {
        return ::fixy::err(SrIovError::ZeroCog);
    }
    if (physical.kind != CogKind::NicPort) {
        return ::fixy::err(SrIovError::NonNicCog);
    }
    if (!interface_name_present(interface)) {
        return ::fixy::err(SrIovError::InvalidInterfaceName);
    }
    return ::fixy::err(SrIovError::QueryDeferred);
}

}  // namespace crucible::cog::sriov

#pragma GCC diagnostic pop
