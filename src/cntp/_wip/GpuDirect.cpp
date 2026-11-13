#include <crucible/cntp/_wip/GpuDirect.h>

#include <utility>

namespace crucible::cntp::_wip::gpu_direct {

::fixy::TextView gpu_direct_error_name(GpuDirectError error) noexcept {
    return ::foundation::core::enum_name_text(error);
}

::fixy::TextView mr_access_flag_name(MrAccessFlag flag) noexcept { return ::foundation::core::enum_name_text(flag); }

::fixy::Result<OwnedGpuDirectMr, GpuDirectError>
GpuDirectMrRegistry::register_gpu_memory(DeclaredGpuDirectMrPlan plan) noexcept {
    auto const& raw = plan.value();
    if (raw.gpu.uuid.is_zero()) {
        return ::fixy::err(GpuDirectError::ZeroGpuCog);
    }
    if (raw.nic.uuid.is_zero()) {
        return ::fixy::err(GpuDirectError::ZeroPeerCog);
    }
    if (raw.gpu_base.value() == 0u) {
        return ::fixy::err(GpuDirectError::NullGpuAddress);
    }
    if (raw.bytes.value() == 0u) {
        return ::fixy::err(GpuDirectError::InvalidByteCount);
    }
    if (!access_valid(raw.access)) {
        return ::fixy::err(GpuDirectError::InvalidAccess);
    }
    if (!raw.peer_module_loaded) {
        return ::fixy::err(GpuDirectError::PeerModuleUnavailable);
    }
    if (!raw.allow_backend_registration) {
        return ::fixy::err(GpuDirectError::RegistrationDeferred);
    }
    return ::fixy::err(GpuDirectError::VendorBackendUnavailable);
}

::fixy::Result<::fixy::Unit, GpuDirectError>
GpuDirectMrRegistry::deregister_gpu_memory(OwnedGpuDirectMr memory) noexcept {
    const GpuDirectMrHandle handle = std::move(memory).consume();
    if (handle.gpu_uuid().is_zero() || handle.nic_uuid().is_zero()) {
        return ::fixy::err(GpuDirectError::ZeroPeerCog);
    }
    return ::fixy::err(GpuDirectError::VendorBackendUnavailable);
}

// The free functions forward to the stub methods of the registry, and each
// carries the same deprecation on itself.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

::fixy::Result<OwnedGpuDirectMr, GpuDirectError> register_gpu_memory(DeclaredGpuDirectMrPlan plan) noexcept {
    GpuDirectMrRegistry registry{};
    return registry.register_gpu_memory(plan);
}

::fixy::Result<::fixy::Unit, GpuDirectError> deregister_gpu_memory(OwnedGpuDirectMr memory) noexcept {
    GpuDirectMrRegistry registry{};
    return registry.deregister_gpu_memory(std::move(memory));
}

::fixy::Result<::fixy::Unit, GpuDirectError> read_from_nvme(DeclaredGpuDirectStoragePlan plan) noexcept {
    auto const& raw = plan.value();
    if (raw.gpu.uuid.is_zero()) {
        return ::fixy::err(GpuDirectError::ZeroGpuCog);
    }
    if (raw.nvme.uuid.is_zero()) {
        return ::fixy::err(GpuDirectError::ZeroPeerCog);
    }
    return ::fixy::err(GpuDirectError::StorageBackendUnavailable);
}

::fixy::Result<::fixy::Unit, GpuDirectError> write_to_nvme(DeclaredGpuDirectStoragePlan plan) noexcept {
    return read_from_nvme(plan);
}

#pragma GCC diagnostic pop

}  // namespace crucible::cntp::_wip::gpu_direct
