#include <crucible/cntp/GpuDirect.h>

namespace crucible::cntp::gpu_direct {

std::string_view gpu_direct_error_name(GpuDirectError error) noexcept {
    switch (error) {
        case GpuDirectError::None:                        return "None";
        case GpuDirectError::ZeroGpuCog:                  return "ZeroGpuCog";
        case GpuDirectError::ZeroPeerCog:                 return "ZeroPeerCog";
        case GpuDirectError::NonGpuCog:                   return "NonGpuCog";
        case GpuDirectError::NonNicCog:                   return "NonNicCog";
        case GpuDirectError::NonNvmeCog:                  return "NonNvmeCog";
        case GpuDirectError::MissingGpuRdmaCapability:
            return "MissingGpuRdmaCapability";
        case GpuDirectError::MissingNicRdmaCapability:
            return "MissingNicRdmaCapability";
        case GpuDirectError::MissingGpuStorageCapability:
            return "MissingGpuStorageCapability";
        case GpuDirectError::PcieRootUnknown:             return "PcieRootUnknown";
        case GpuDirectError::PcieRootMismatch:            return "PcieRootMismatch";
        case GpuDirectError::NullGpuAddress:              return "NullGpuAddress";
        case GpuDirectError::InvalidByteCount:            return "InvalidByteCount";
        case GpuDirectError::InvalidAccess:               return "InvalidAccess";
        case GpuDirectError::PeerModuleUnavailable:
            return "PeerModuleUnavailable";
        case GpuDirectError::RegistrationDeferred:
            return "RegistrationDeferred";
        case GpuDirectError::VendorBackendUnavailable:
            return "VendorBackendUnavailable";
        case GpuDirectError::StorageBackendUnavailable:
            return "StorageBackendUnavailable";
        default:                                          return "<unknown GpuDirectError>";
    }
}

std::string_view mr_access_flag_name(MrAccessFlag flag) noexcept {
    switch (flag) {
        case MrAccessFlag::LocalRead:    return "LocalRead";
        case MrAccessFlag::LocalWrite:   return "LocalWrite";
        case MrAccessFlag::RemoteRead:   return "RemoteRead";
        case MrAccessFlag::RemoteWrite:  return "RemoteWrite";
        case MrAccessFlag::RemoteAtomic: return "RemoteAtomic";
        default:                         return "<unknown MrAccessFlag>";
    }
}

std::expected<GpuDirectMrHandle, GpuDirectError>
GpuDirectMrRegistry::register_gpu_memory(
    DeclaredGpuDirectMrPlan plan) noexcept {
    auto const& raw = plan.value();
    if (raw.gpu.uuid.is_zero()) {
        return std::unexpected(GpuDirectError::ZeroGpuCog);
    }
    if (raw.nic.uuid.is_zero()) {
        return std::unexpected(GpuDirectError::ZeroPeerCog);
    }
    if (raw.gpu_base.value() == 0u) {
        return std::unexpected(GpuDirectError::NullGpuAddress);
    }
    if (raw.bytes.value() == 0u) {
        return std::unexpected(GpuDirectError::InvalidByteCount);
    }
    if (!access_valid(raw.access)) {
        return std::unexpected(GpuDirectError::InvalidAccess);
    }
    if (!raw.peer_module_loaded) {
        return std::unexpected(GpuDirectError::PeerModuleUnavailable);
    }
    if (!raw.allow_backend_registration) {
        return std::unexpected(GpuDirectError::RegistrationDeferred);
    }
    return std::unexpected(GpuDirectError::VendorBackendUnavailable);
}

std::expected<void, GpuDirectError>
GpuDirectMrRegistry::deregister_gpu_memory(
    GpuDirectMrHandle handle) noexcept {
    if (handle.gpu_uuid.is_zero() || handle.nic_uuid.is_zero()) {
        return std::unexpected(GpuDirectError::ZeroPeerCog);
    }
    return std::unexpected(GpuDirectError::VendorBackendUnavailable);
}

std::expected<GpuDirectMrHandle, GpuDirectError>
register_gpu_memory(DeclaredGpuDirectMrPlan plan) noexcept {
    GpuDirectMrRegistry registry{};
    return registry.register_gpu_memory(plan);
}

std::expected<void, GpuDirectError>
deregister_gpu_memory(GpuDirectMrHandle handle) noexcept {
    GpuDirectMrRegistry registry{};
    return registry.deregister_gpu_memory(handle);
}

std::expected<void, GpuDirectError>
read_from_nvme(DeclaredGpuDirectStoragePlan plan) noexcept {
    auto const& raw = plan.value();
    if (raw.gpu.uuid.is_zero()) {
        return std::unexpected(GpuDirectError::ZeroGpuCog);
    }
    if (raw.nvme.uuid.is_zero()) {
        return std::unexpected(GpuDirectError::ZeroPeerCog);
    }
    if (!raw.storage_backend_loaded || !raw.allow_backend_io) {
        return std::unexpected(GpuDirectError::StorageBackendUnavailable);
    }
    return std::unexpected(GpuDirectError::StorageBackendUnavailable);
}

std::expected<void, GpuDirectError>
write_to_nvme(DeclaredGpuDirectStoragePlan plan) noexcept {
    return read_from_nvme(plan);
}

}  // namespace crucible::cntp::gpu_direct
