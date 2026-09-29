#pragma once

// Nothing here calls CUDA, HIP, cuFile, libibverbs or a kernel peer module.
// The registration and I/O entrypoints declared at the bottom prove the plan
// shape and then report deferral or unavailability.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/OffloadTarget.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Bits.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>

#include <bit>
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
    LocalRead = 1u << 0,
    LocalWrite = 1u << 1,
    RemoteRead = 1u << 2,
    RemoteWrite = 1u << 3,
    RemoteAtomic = 1u << 4,
};

[[nodiscard]] std::string_view gpu_direct_error_name(GpuDirectError error) noexcept;
[[nodiscard]] std::string_view mr_access_flag_name(MrAccessFlag flag) noexcept;

using GpuVirtualAddress = ::fixy::Refined<::fixy::non_zero, std::uintptr_t>;
using GpuDirectByteCount = ::fixy::Positive<std::uint64_t>;
using StorageByteOffset = std::uint64_t;
using PcieRootId = ::fixy::Tagged<std::uint16_t, ::fixy::tags::source::Vendor>;
using MrAccess = ::fixy::Bits<MrAccessFlag>;

inline constexpr std::uint16_t kUnknownPcieRootId = 0xffffu;

struct PeerPlacement {
    PcieRootId gpu_pcie_root = ::fixy::mint_tagged<::fixy::tags::source::Vendor>(kUnknownPcieRootId);
    PcieRootId peer_pcie_root = ::fixy::mint_tagged<::fixy::tags::source::Vendor>(kUnknownPcieRootId);
    // Set when a peer-to-peer bridge makes cross-root access legal.  Topology
    // discovery supplies this.  It is never inferred here.
    bool peer_bridge_present = false;
};

// The address and the byte count take no default.  A plan names the values
// that its caller admitted, so no plan exists before those values do.
struct GpuDirectMrPlan {
    cog::CogIdentity gpu{};
    cog::CogIdentity nic{};
    PeerPlacement placement{};
    GpuVirtualAddress gpu_base;
    GpuDirectByteCount bytes;
    MrAccess access{MrAccessFlag::LocalWrite, MrAccessFlag::RemoteWrite};
    bool peer_module_loaded = false;
    bool allow_backend_registration = false;
};

struct GpuDirectStoragePlan {
    cog::CogIdentity gpu{};
    cog::CogIdentity nvme{};
    PeerPlacement placement{};
    GpuVirtualAddress gpu_base;
    GpuDirectByteCount bytes;
    StorageByteOffset storage_offset_bytes = 0;
    bool storage_backend_loaded = false;
    bool allow_backend_io = false;
};

// mint_gpu_direct_mr_plan and mint_gpu_direct_storage_plan are the one
// functions that return these declared plans.
using DeclaredGpuDirectMrPlan = ::fixy::Tagged<GpuDirectMrPlan, wip_source::GpuDirect>;
using DeclaredGpuDirectStoragePlan = ::fixy::Tagged<GpuDirectStoragePlan, wip_source::GpuDirect>;

class GpuDirectMrRegistry;

// The handle of a memory region that a NIC reaches in GPU memory.  The
// constructor is private and the registry is its one door, so no caller
// holds a region that no registration made.  The door opens for no plan
// while no backend exists.  A handle names one region, so it moves and does
// not copy: a copy would let a second owner be minted from the first.
class GpuDirectMrHandle {
public:
    GpuDirectMrHandle(GpuDirectMrHandle const&) = delete("a memory region handle names one registered region");
    GpuDirectMrHandle&
    operator=(GpuDirectMrHandle const&) = delete("a memory region handle names one registered region");
    GpuDirectMrHandle(GpuDirectMrHandle&&) noexcept = default;
    GpuDirectMrHandle& operator=(GpuDirectMrHandle&&) noexcept = default;
    ~GpuDirectMrHandle() = default;

    [[nodiscard]] constexpr cog::Uuid gpu_uuid() const noexcept { return gpu_uuid_; }
    [[nodiscard]] constexpr cog::Uuid nic_uuid() const noexcept { return nic_uuid_; }
    [[nodiscard]] constexpr GpuVirtualAddress gpu_base() const noexcept { return gpu_base_; }
    [[nodiscard]] constexpr GpuDirectByteCount bytes() const noexcept { return bytes_; }
    [[nodiscard]] constexpr MrAccess access() const noexcept { return access_; }

private:
    constexpr GpuDirectMrHandle(cog::Uuid gpu_uuid, cog::Uuid nic_uuid, GpuVirtualAddress gpu_base,
                                GpuDirectByteCount bytes, MrAccess access) noexcept
        : gpu_uuid_{gpu_uuid}, nic_uuid_{nic_uuid}, gpu_base_{gpu_base}, bytes_{bytes}, access_{access} {}

    friend class GpuDirectMrRegistry;

    cog::Uuid gpu_uuid_{};
    cog::Uuid nic_uuid_{};
    GpuVirtualAddress gpu_base_;
    GpuDirectByteCount bytes_;
    MrAccess access_{};
};

using OwnedGpuDirectMr = ::fixy::Linear<GpuDirectMrHandle>;

template <class Ctx>
concept CtxFitsGpuDirectMint = ::foundation::effects::IsExecCtx<Ctx>
                            && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

[[nodiscard]] constexpr std::expected<GpuVirtualAddress, GpuDirectError>
admit_gpu_virtual_address(std::uintptr_t address) noexcept {
    return ::fixy::admit_refined<::fixy::non_zero>(address, GpuDirectError::NullGpuAddress);
}

[[nodiscard]] inline std::expected<GpuVirtualAddress, GpuDirectError>
admit_gpu_virtual_address(void const* address) noexcept {
    return admit_gpu_virtual_address(std::bit_cast<std::uintptr_t>(address));
}

[[nodiscard]] constexpr std::expected<GpuDirectByteCount, GpuDirectError>
admit_gpu_direct_bytes(std::uint64_t bytes) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(bytes, GpuDirectError::InvalidByteCount);
}

[[nodiscard]] constexpr MrAccess mrc_write_access() noexcept {
    return MrAccess{MrAccessFlag::LocalWrite, MrAccessFlag::RemoteWrite};
}

[[nodiscard]] constexpr bool access_valid(MrAccess access) noexcept { return !access.none(); }

[[nodiscard]] constexpr bool placement_compatible(PeerPlacement placement) noexcept {
    return placement.peer_bridge_present || placement.gpu_pcie_root.value() == placement.peer_pcie_root.value();
}

[[nodiscard]] constexpr bool placement_known(PeerPlacement placement) noexcept {
    return placement.gpu_pcie_root.value() != kUnknownPcieRootId
        && placement.peer_pcie_root.value() != kUnknownPcieRootId;
}

// A GPU that takes part in peer DMA with a NIC advertises GPUDirect RDMA.
inline constexpr cog::OffloadTargetRefusals<GpuDirectError> gpu_rdma_target_refusals{
    .undiscovered = GpuDirectError::ZeroGpuCog,
    .wrong_kind = GpuDirectError::NonGpuCog,
    .missing_feature = GpuDirectError::MissingGpuRdmaCapability,
};

// A GPU that takes part in peer DMA with an NVMe device advertises
// GPUDirect Storage.
inline constexpr cog::OffloadTargetRefusals<GpuDirectError> gpu_storage_target_refusals{
    .undiscovered = GpuDirectError::ZeroGpuCog,
    .wrong_kind = GpuDirectError::NonGpuCog,
    .missing_feature = GpuDirectError::MissingGpuStorageCapability,
};

// The NIC port of a peer DMA advertises GPUDirect RDMA too.
inline constexpr cog::OffloadTargetRefusals<GpuDirectError> nic_rdma_target_refusals{
    .undiscovered = GpuDirectError::ZeroPeerCog,
    .wrong_kind = GpuDirectError::NonNicCog,
    .missing_feature = GpuDirectError::MissingNicRdmaCapability,
};

// An NVMe peer has no capability schema, so its check reads the identity
// alone.
inline constexpr cog::CogTargetRefusals<GpuDirectError> nvme_target_refusals{
    .undiscovered = GpuDirectError::ZeroPeerCog,
    .wrong_kind = GpuDirectError::NonNvmeCog,
};

[[nodiscard]] constexpr std::expected<void, GpuDirectError>
validate_gpu_for_rdma(cog::CogIdentity const& gpu, cog::GpuTargetCaps const& caps) noexcept {
    return cog::validate_offload_target<cog::GpuFeature::GpuDirectRdma, cog::CogKind::Gpu>(gpu, caps,
                                                                                           gpu_rdma_target_refusals);
}

[[nodiscard]] constexpr std::expected<void, GpuDirectError>
validate_gpu_for_storage(cog::CogIdentity const& gpu, cog::GpuTargetCaps const& caps) noexcept {
    return cog::validate_offload_target<cog::GpuFeature::GpuDirectStorage, cog::CogKind::Gpu>(
        gpu, caps, gpu_storage_target_refusals);
}

[[nodiscard]] constexpr std::expected<void, GpuDirectError>
validate_nic_for_rdma(cog::CogIdentity const& nic, cog::NicPortTargetCaps const& caps) noexcept {
    return cog::validate_offload_target<cog::NicFeature::GpuDirectRdma, cog::CogKind::NicPort>(
        nic, caps, nic_rdma_target_refusals);
}

[[nodiscard]] constexpr std::expected<void, GpuDirectError> validate_nvme_peer(cog::CogIdentity const& nvme) noexcept {
    return cog::validate_cog_target<cog::CogKind::NvmeNamespace, cog::CogKind::NvmeDrive>(nvme, nvme_target_refusals);
}

[[nodiscard]] constexpr std::expected<void, GpuDirectError>
check_gpu_nic_compat(cog::CogIdentity const& gpu, cog::GpuTargetCaps const& gpu_caps, cog::CogIdentity const& nic,
                     cog::NicPortTargetCaps const& nic_caps, PeerPlacement placement) noexcept {
    auto gpu_valid = validate_gpu_for_rdma(gpu, gpu_caps);
    if (!gpu_valid.has_value()) {
        return gpu_valid;
    }
    auto nic_valid = validate_nic_for_rdma(nic, nic_caps);
    if (!nic_valid.has_value()) {
        return nic_valid;
    }
    if (!placement.peer_bridge_present && !placement_known(placement)) {
        return std::unexpected(GpuDirectError::PcieRootUnknown);
    }
    if (!placement_compatible(placement)) {
        return std::unexpected(GpuDirectError::PcieRootMismatch);
    }
    return {};
}

// The refined fields already hold the address and byte bounds.  The check
// reads them again, so a value that entered through mint_refined_trusted is
// still refused here.
[[nodiscard]] constexpr std::expected<void, GpuDirectError>
validate_mr_plan(GpuDirectMrPlan const& plan, cog::GpuTargetCaps const& gpu_caps,
                 cog::NicPortTargetCaps const& nic_caps) noexcept {
    auto compat = check_gpu_nic_compat(plan.gpu, gpu_caps, plan.nic, nic_caps, plan.placement);
    if (!compat.has_value()) {
        return compat;
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
validate_storage_plan(GpuDirectStoragePlan const& plan, cog::GpuTargetCaps const& gpu_caps) noexcept {
    auto gpu_valid = validate_gpu_for_storage(plan.gpu, gpu_caps);
    if (!gpu_valid.has_value()) {
        return gpu_valid;
    }
    auto nvme_valid = validate_nvme_peer(plan.nvme);
    if (!nvme_valid.has_value()) {
        return nvme_valid;
    }
    if (!plan.placement.peer_bridge_present && !placement_known(plan.placement)) {
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
mint_gpu_direct_mr_plan(Ctx const&, cog::CogIdentity gpu, cog::GpuTargetCaps const& gpu_caps, cog::CogIdentity nic,
                        cog::NicPortTargetCaps const& nic_caps, PeerPlacement placement, GpuVirtualAddress gpu_base,
                        GpuDirectByteCount bytes, MrAccess access = mrc_write_access(), bool peer_module_loaded = false,
                        bool allow_backend_registration = false) noexcept {
    const GpuDirectMrPlan plan{
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
    return ::fixy::mint_tagged<wip_source::GpuDirect>(plan);
}

template <class Ctx>
    requires CtxFitsGpuDirectMint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredGpuDirectStoragePlan, GpuDirectError>
mint_gpu_direct_storage_plan(Ctx const&, cog::CogIdentity gpu, cog::GpuTargetCaps const& gpu_caps,
                             cog::CogIdentity nvme, PeerPlacement placement, GpuVirtualAddress gpu_base,
                             GpuDirectByteCount bytes, StorageByteOffset storage_offset_bytes = 0,
                             bool storage_backend_loaded = false, bool allow_backend_io = false) noexcept {
    const GpuDirectStoragePlan plan{
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
    return ::fixy::mint_tagged<wip_source::GpuDirect>(plan);
}

// The registry is the one door of a region handle.  A region that a
// registration returns is owned once, and a deregistration consumes it.
class GpuDirectMrRegistry : public ::foundation::Pinned<GpuDirectMrRegistry> {
public:
    GpuDirectMrRegistry() = default;

    [[nodiscard]] std::expected<OwnedGpuDirectMr, GpuDirectError>
    register_gpu_memory(DeclaredGpuDirectMrPlan plan) noexcept;

    [[nodiscard]] std::expected<void, GpuDirectError> deregister_gpu_memory(OwnedGpuDirectMr memory) noexcept;
};

[[nodiscard]] std::expected<OwnedGpuDirectMr, GpuDirectError>
register_gpu_memory(DeclaredGpuDirectMrPlan plan) noexcept;

[[nodiscard]] std::expected<void, GpuDirectError> deregister_gpu_memory(OwnedGpuDirectMr memory) noexcept;

[[nodiscard]] std::expected<void, GpuDirectError> read_from_nvme(DeclaredGpuDirectStoragePlan plan) noexcept;

[[nodiscard]] std::expected<void, GpuDirectError> write_to_nvme(DeclaredGpuDirectStoragePlan plan) noexcept;

static_assert(sizeof(GpuVirtualAddress) == sizeof(std::uintptr_t));
static_assert(sizeof(GpuDirectByteCount) == sizeof(std::uint64_t));
static_assert(sizeof(PcieRootId) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredGpuDirectMrPlan) == sizeof(GpuDirectMrPlan));
static_assert(sizeof(DeclaredGpuDirectStoragePlan) == sizeof(GpuDirectStoragePlan));
static_assert(::fixy::qtt_consume_tracked || sizeof(OwnedGpuDirectMr) == sizeof(GpuDirectMrHandle));
static_assert(CtxFitsGpuDirectMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsGpuDirectMint<::fixy::BgDrainCtx>);
static_assert(std::is_trivially_copyable_v<PeerPlacement>);
// A refined member makes a plan not trivially copyable, because no byte
// route may build a refined value.  A copy still costs what copying the
// bytes costs.
static_assert(std::is_trivially_copy_constructible_v<GpuDirectMrPlan>
              && std::is_trivially_destructible_v<GpuDirectMrPlan>);
static_assert(std::is_trivially_copy_constructible_v<GpuDirectStoragePlan>
              && std::is_trivially_destructible_v<GpuDirectStoragePlan>);
// No declared plan exists before its address and byte count, and no region
// handle exists outside the registry or beside the one it names.
static_assert(!std::is_default_constructible_v<DeclaredGpuDirectMrPlan>);
static_assert(!std::is_default_constructible_v<DeclaredGpuDirectStoragePlan>);
static_assert(
    !std::is_constructible_v<GpuDirectMrHandle, cog::Uuid, cog::Uuid, GpuVirtualAddress, GpuDirectByteCount, MrAccess>);
static_assert(!std::is_copy_constructible_v<GpuDirectMrHandle>
              && std::is_nothrow_move_constructible_v<GpuDirectMrHandle>);

}  // namespace crucible::cntp::_wip::gpu_direct
