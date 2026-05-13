#pragma once

// GAPS-132 WIP. CNT-P GPUDirect RDMA / Storage sketch.
//
// This header owns typed eligibility and registration intent for GPU-peer DMA.
// It deliberately does not call CUDA, HIP, cuFile, libibverbs, or kernel peer
// modules. Live backends must consume DeclaredGpuDirect* plans and currently
// report explicit deferral / unavailability after the request shape is proven.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cntp::_wip::gpu_direct {

namespace wip_source {
struct GpuDirect {};
}  // namespace wip_source

enum class GpuDirectError : std::uint8_t {
    None = 0,
    ZeroGpuCog,
    ZeroPeerCog,
    NonGpuCog,
    NonNicCog,
    NonNvmeCog,
    MissingGpuRdmaCapability,
    MissingNicRdmaCapability,
    MissingGpuStorageCapability,
    PcieRootUnknown,
    PcieRootMismatch,
    NullGpuAddress,
    InvalidByteCount,
    InvalidAccess,
    PeerModuleUnavailable,
    RegistrationDeferred,
    VendorBackendUnavailable,
    StorageBackendUnavailable,
};

enum class MrAccessFlag : std::uint8_t {
    LocalRead   = 1u << 0,
    LocalWrite  = 1u << 1,
    RemoteRead  = 1u << 2,
    RemoteWrite = 1u << 3,
    RemoteAtomic= 1u << 4,
};

[[nodiscard]] std::string_view
gpu_direct_error_name(GpuDirectError error) noexcept;
[[nodiscard]] std::string_view mr_access_flag_name(MrAccessFlag flag) noexcept;

using GpuVirtualAddress = safety::Refined<safety::non_zero, std::uintptr_t>;
using GpuDirectByteCount = safety::Positive<std::uint64_t>;
using StorageByteOffset = std::uint64_t;
using PcieRootId = safety::Tagged<std::uint16_t, safety::source::Vendor>;
using MrAccess = safety::Bits<MrAccessFlag>;

inline constexpr std::uint16_t kUnknownPcieRootId = 0xffffu;

struct PeerPlacement {
    PcieRootId gpu_pcie_root{std::uint16_t{kUnknownPcieRootId}};
    PcieRootId peer_pcie_root{std::uint16_t{kUnknownPcieRootId}};
    // True for a discovered peer-to-peer bridge that makes cross-root access
    // legal. This is supplied by topology discovery, not inferred here.
    bool peer_bridge_present = false;
};

struct GpuDirectMrPlan {
    cog::CogIdentity gpu{};
    cog::CogIdentity nic{};
    PeerPlacement placement{};
    GpuVirtualAddress gpu_base{std::uintptr_t{1},
                               typename GpuVirtualAddress::Trusted{}};
    GpuDirectByteCount bytes{std::uint64_t{1}};
    MrAccess access{MrAccessFlag::LocalWrite, MrAccessFlag::RemoteWrite};
    bool peer_module_loaded = false;
    bool allow_backend_registration = false;
};

struct GpuDirectStoragePlan {
    cog::CogIdentity gpu{};
    cog::CogIdentity nvme{};
    PeerPlacement placement{};
    GpuVirtualAddress gpu_base{std::uintptr_t{1},
                               typename GpuVirtualAddress::Trusted{}};
    GpuDirectByteCount bytes{std::uint64_t{1}};
    StorageByteOffset storage_offset_bytes = 0;
    bool storage_backend_loaded = false;
    bool allow_backend_io = false;
};

struct GpuDirectMrHandle {
    cog::Uuid gpu_uuid{};
    cog::Uuid nic_uuid{};
    GpuVirtualAddress gpu_base{std::uintptr_t{1},
                               typename GpuVirtualAddress::Trusted{}};
    GpuDirectByteCount bytes{std::uint64_t{1}};
    MrAccess access{MrAccessFlag::LocalWrite, MrAccessFlag::RemoteWrite};
};

using DeclaredGpuDirectMrPlan =
    safety::Tagged<GpuDirectMrPlan, wip_source::GpuDirect>;
using DeclaredGpuDirectStoragePlan =
    safety::Tagged<GpuDirectStoragePlan, wip_source::GpuDirect>;

template <class Ctx>
concept CtxFitsGpuDirectMint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

[[nodiscard]] constexpr std::expected<GpuVirtualAddress, GpuDirectError>
admit_gpu_virtual_address(std::uintptr_t address) noexcept {
    if (address == 0u) {
        return std::unexpected(GpuDirectError::NullGpuAddress);
    }
    return GpuVirtualAddress{
        address, typename GpuVirtualAddress::Trusted{}};
}

[[nodiscard]] inline std::expected<GpuVirtualAddress, GpuDirectError>
admit_gpu_virtual_address(void const* address) noexcept {
    return admit_gpu_virtual_address(
        reinterpret_cast<std::uintptr_t>(address));
}

[[nodiscard]] constexpr std::expected<GpuDirectByteCount, GpuDirectError>
admit_gpu_direct_bytes(std::uint64_t bytes) noexcept {
    if (bytes == 0u) {
        return std::unexpected(GpuDirectError::InvalidByteCount);
    }
    return GpuDirectByteCount{bytes, typename GpuDirectByteCount::Trusted{}};
}

[[nodiscard]] constexpr MrAccess mrc_write_access() noexcept {
    return MrAccess{MrAccessFlag::LocalWrite, MrAccessFlag::RemoteWrite};
}

[[nodiscard]] constexpr bool access_valid(MrAccess access) noexcept {
    return !access.none();
}

[[nodiscard]] constexpr bool placement_compatible(
    PeerPlacement placement) noexcept {
    return placement.peer_bridge_present
        || placement.gpu_pcie_root.value() == placement.peer_pcie_root.value();
}

[[nodiscard]] constexpr bool placement_known(PeerPlacement placement) noexcept {
    return placement.gpu_pcie_root.value() != kUnknownPcieRootId
        && placement.peer_pcie_root.value() != kUnknownPcieRootId;
}

[[nodiscard]] constexpr std::expected<void, GpuDirectError>
validate_gpu_for_rdma(cog::CogIdentity gpu,
                      cog::GpuTargetCaps const& caps) noexcept {
    if (gpu.uuid.is_zero()) {
        return std::unexpected(GpuDirectError::ZeroGpuCog);
    }
    if (gpu.kind != cog::CogKind::Gpu) {
        return std::unexpected(GpuDirectError::NonGpuCog);
    }
    if (!caps.features.test(cog::GpuFeature::GpuDirectRdma)) {
        return std::unexpected(GpuDirectError::MissingGpuRdmaCapability);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, GpuDirectError>
validate_gpu_for_storage(cog::CogIdentity gpu,
                         cog::GpuTargetCaps const& caps) noexcept {
    if (gpu.uuid.is_zero()) {
        return std::unexpected(GpuDirectError::ZeroGpuCog);
    }
    if (gpu.kind != cog::CogKind::Gpu) {
        return std::unexpected(GpuDirectError::NonGpuCog);
    }
    if (!caps.features.test(cog::GpuFeature::GpuDirectStorage)) {
        return std::unexpected(GpuDirectError::MissingGpuStorageCapability);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, GpuDirectError>
validate_nic_for_rdma(cog::CogIdentity nic,
                      cog::NicPortTargetCaps const& caps) noexcept {
    if (nic.uuid.is_zero()) {
        return std::unexpected(GpuDirectError::ZeroPeerCog);
    }
    if (nic.kind != cog::CogKind::NicPort) {
        return std::unexpected(GpuDirectError::NonNicCog);
    }
    if (!caps.features.test(cog::NicFeature::GpuDirectRdma)) {
        return std::unexpected(GpuDirectError::MissingNicRdmaCapability);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, GpuDirectError>
validate_nvme_peer(cog::CogIdentity nvme) noexcept {
    if (nvme.uuid.is_zero()) {
        return std::unexpected(GpuDirectError::ZeroPeerCog);
    }
    if (nvme.kind != cog::CogKind::NvmeNamespace
        && nvme.kind != cog::CogKind::NvmeDrive) {
        return std::unexpected(GpuDirectError::NonNvmeCog);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, GpuDirectError>
check_gpu_nic_compat(cog::CogIdentity gpu,
                     cog::GpuTargetCaps const& gpu_caps,
                     cog::CogIdentity nic,
                     cog::NicPortTargetCaps const& nic_caps,
                     PeerPlacement placement) noexcept {
    auto gpu_valid = validate_gpu_for_rdma(gpu, gpu_caps);
    if (!gpu_valid.has_value()) {
        return std::unexpected(gpu_valid.error());
    }
    auto nic_valid = validate_nic_for_rdma(nic, nic_caps);
    if (!nic_valid.has_value()) {
        return std::unexpected(nic_valid.error());
    }
    if (!placement.peer_bridge_present && !placement_known(placement)) {
        return std::unexpected(GpuDirectError::PcieRootUnknown);
    }
    if (!placement_compatible(placement)) {
        return std::unexpected(GpuDirectError::PcieRootMismatch);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, GpuDirectError>
validate_mr_plan(GpuDirectMrPlan const& plan,
                 cog::GpuTargetCaps const& gpu_caps,
                 cog::NicPortTargetCaps const& nic_caps) noexcept {
    auto compat = check_gpu_nic_compat(
        plan.gpu, gpu_caps, plan.nic, nic_caps, plan.placement);
    if (!compat.has_value()) {
        return std::unexpected(compat.error());
    }
    if (plan.gpu_base.value() == 0u) {
        return std::unexpected(GpuDirectError::NullGpuAddress);
    }
    if (plan.bytes.value() == 0u) {
        return std::unexpected(GpuDirectError::InvalidByteCount);
    }
    if (!access_valid(plan.access)) {
        return std::unexpected(GpuDirectError::InvalidAccess);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, GpuDirectError>
validate_storage_plan(GpuDirectStoragePlan const& plan,
                      cog::GpuTargetCaps const& gpu_caps) noexcept {
    auto gpu_valid = validate_gpu_for_storage(plan.gpu, gpu_caps);
    if (!gpu_valid.has_value()) {
        return std::unexpected(gpu_valid.error());
    }
    auto nvme_valid = validate_nvme_peer(plan.nvme);
    if (!nvme_valid.has_value()) {
        return std::unexpected(nvme_valid.error());
    }
    if (!plan.placement.peer_bridge_present
        && !placement_known(plan.placement)) {
        return std::unexpected(GpuDirectError::PcieRootUnknown);
    }
    if (!placement_compatible(plan.placement)) {
        return std::unexpected(GpuDirectError::PcieRootMismatch);
    }
    if (plan.gpu_base.value() == 0u) {
        return std::unexpected(GpuDirectError::NullGpuAddress);
    }
    if (plan.bytes.value() == 0u) {
        return std::unexpected(GpuDirectError::InvalidByteCount);
    }
    return {};
}

template <class Ctx>
    requires CtxFitsGpuDirectMint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredGpuDirectMrPlan, GpuDirectError>
mint_gpu_direct_mr_plan(Ctx const&,
                        cog::CogIdentity gpu,
                        cog::GpuTargetCaps gpu_caps,
                        cog::CogIdentity nic,
                        cog::NicPortTargetCaps nic_caps,
                        PeerPlacement placement,
                        GpuVirtualAddress gpu_base,
                        GpuDirectByteCount bytes,
                        MrAccess access = mrc_write_access(),
                        bool peer_module_loaded = false,
                        bool allow_backend_registration = false) noexcept {
    GpuDirectMrPlan plan{
        .gpu = gpu,
        .nic = nic,
        .placement = placement,
        .gpu_base = gpu_base,
        .bytes = bytes,
        .access = access,
        .peer_module_loaded = peer_module_loaded,
        .allow_backend_registration = allow_backend_registration,
    };
    auto valid = validate_mr_plan(plan, gpu_caps, nic_caps);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return DeclaredGpuDirectMrPlan{plan};
}

template <class Ctx>
    requires CtxFitsGpuDirectMint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredGpuDirectStoragePlan,
                                      GpuDirectError>
mint_gpu_direct_storage_plan(Ctx const&,
                             cog::CogIdentity gpu,
                             cog::GpuTargetCaps gpu_caps,
                             cog::CogIdentity nvme,
                             PeerPlacement placement,
                             GpuVirtualAddress gpu_base,
                             GpuDirectByteCount bytes,
                             StorageByteOffset storage_offset_bytes = 0,
                             bool storage_backend_loaded = false,
                             bool allow_backend_io = false) noexcept {
    GpuDirectStoragePlan plan{
        .gpu = gpu,
        .nvme = nvme,
        .placement = placement,
        .gpu_base = gpu_base,
        .bytes = bytes,
        .storage_offset_bytes = storage_offset_bytes,
        .storage_backend_loaded = storage_backend_loaded,
        .allow_backend_io = allow_backend_io,
    };
    auto valid = validate_storage_plan(plan, gpu_caps);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return DeclaredGpuDirectStoragePlan{plan};
}

class GpuDirectMrRegistry : public safety::Pinned<GpuDirectMrRegistry> {
public:
    GpuDirectMrRegistry() = default;

    [[nodiscard]] std::expected<GpuDirectMrHandle, GpuDirectError>
    register_gpu_memory(DeclaredGpuDirectMrPlan plan) noexcept;

    [[nodiscard]] std::expected<void, GpuDirectError>
    deregister_gpu_memory(GpuDirectMrHandle handle) noexcept;
};

[[nodiscard]] std::expected<GpuDirectMrHandle, GpuDirectError>
register_gpu_memory(DeclaredGpuDirectMrPlan plan) noexcept;

[[nodiscard]] std::expected<void, GpuDirectError>
deregister_gpu_memory(GpuDirectMrHandle handle) noexcept;

[[nodiscard]] std::expected<void, GpuDirectError>
read_from_nvme(DeclaredGpuDirectStoragePlan plan) noexcept;

[[nodiscard]] std::expected<void, GpuDirectError>
write_to_nvme(DeclaredGpuDirectStoragePlan plan) noexcept;

static_assert(sizeof(GpuVirtualAddress) == sizeof(std::uintptr_t));
static_assert(sizeof(GpuDirectByteCount) == sizeof(std::uint64_t));
static_assert(sizeof(PcieRootId) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredGpuDirectMrPlan) == sizeof(GpuDirectMrPlan));
static_assert(sizeof(DeclaredGpuDirectStoragePlan)
              == sizeof(GpuDirectStoragePlan));
static_assert(CtxFitsGpuDirectMint<effects::ColdInitCtx>);
static_assert(!CtxFitsGpuDirectMint<effects::BgDrainCtx>);
static_assert(std::is_trivially_copyable_v<PeerPlacement>);
static_assert(std::is_trivially_copyable_v<GpuDirectMrPlan>);
static_assert(std::is_trivially_copyable_v<GpuDirectStoragePlan>);
static_assert(std::is_trivially_copyable_v<GpuDirectMrHandle>);

}  // namespace crucible::cntp::_wip::gpu_direct
