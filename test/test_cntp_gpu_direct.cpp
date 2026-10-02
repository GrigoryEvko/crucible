#include <crucible/cntp/_wip/GpuDirect.h>
#include <fixy/Ctx.h>

#include "test_assert.h"
#include <concepts>
#include <cstdio>
#include <string_view>
#include <type_traits>

// The registration and storage calls carry a CRUCIBLE_STUB deprecation,
// because no driver stands behind them.  This file calls them on purpose to
// pin their sentinel errors, so the warning is suppressed for this file alone.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace cog = crucible::cog;
namespace fe = ::foundation::effects;
namespace gd = crucible::cntp::_wip::gpu_direct;

namespace {

cog::CogIdentity gpu_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x132, 0x600};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::Gpu;
    return id;
}

cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x132, 0x710};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

cog::CogIdentity nvme_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x132, 0x510};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NvmeNamespace;
    return id;
}

cog::GpuTargetCaps gpu_caps() {
    cog::GpuTargetCaps caps{};
    caps.features.set(cog::GpuFeature::GpuDirectRdma);
    caps.features.set(cog::GpuFeature::GpuDirectStorage);
    return caps;
}

cog::NicPortTargetCaps nic_caps() {
    cog::NicPortTargetCaps caps{};
    caps.features.set(cog::NicFeature::GpuDirectRdma);
    caps.max_mr_count = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint32_t>(4096);
    caps.max_mr_size_bytes = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(1ull << 40u);
    return caps;
}

gd::PcieRootId pcie_root(std::uint16_t root) { return ::fixy::mint_tagged<::fixy::tags::source::Vendor>(root); }

gd::PeerPlacement same_root() {
    return gd::PeerPlacement{
        .gpu_pcie_root = pcie_root(7),
        .peer_pcie_root = pcie_root(7),
        .peer_bridge_present = false,
    };
}

void test_admission_and_names() {
    assert(gd::gpu_direct_error_name(gd::GpuDirectError::PcieRootMismatch) == std::string_view{"PcieRootMismatch"});
    assert(gd::mr_access_flag_name(gd::MrAccessFlag::RemoteWrite) == std::string_view{"RemoteWrite"});

    auto address = gd::admit_gpu_virtual_address(0x1000u);
    assert(address.has_value());
    assert(address->value() == 0x1000u);

    auto null_address = gd::admit_gpu_virtual_address(std::uintptr_t{0});
    assert(!null_address.has_value());
    assert(null_address.error() == gd::GpuDirectError::NullGpuAddress);

    auto null_pointer = gd::admit_gpu_virtual_address(static_cast<void const*>(nullptr));
    assert(!null_pointer.has_value());
    assert(null_pointer.error() == gd::GpuDirectError::NullGpuAddress);

    auto bytes = gd::admit_gpu_direct_bytes(4096);
    assert(bytes.has_value());
    assert(bytes->value() == 4096);

    auto zero_bytes = gd::admit_gpu_direct_bytes(0);
    assert(!zero_bytes.has_value());
    assert(zero_bytes.error() == gd::GpuDirectError::InvalidByteCount);

    assert(gd::mrc_write_access().test(gd::MrAccessFlag::LocalWrite));
    assert(gd::mrc_write_access().test(gd::MrAccessFlag::RemoteWrite));
    assert(!gd::placement_known(gd::PeerPlacement{}));

    crucible::test::pass("  test_admission_and_names: PASSED\n");
}

void test_rdma_plan_minting() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), same_root(),
                                    *gd::admit_gpu_virtual_address(0x2000u), *gd::admit_gpu_direct_bytes(1u << 20u));
    assert(plan.has_value());
    static_assert(std::same_as<std::remove_cvref_t<decltype(*plan)>, gd::DeclaredGpuDirectMrPlan>);
    assert(plan->value().gpu.uuid == gpu_identity().uuid);
    assert(plan->value().nic.uuid == nic_identity().uuid);
    assert(plan->value().bytes.value() == (1u << 20u));

    gd::PeerPlacement remote_root = same_root();
    remote_root.peer_pcie_root = pcie_root(8);
    auto mismatch =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), remote_root,
                                    *gd::admit_gpu_virtual_address(0x2000u), *gd::admit_gpu_direct_bytes(4096));
    assert(!mismatch.has_value());
    assert(mismatch.error() == gd::GpuDirectError::PcieRootMismatch);

    remote_root.peer_bridge_present = true;
    auto bridged =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), remote_root,
                                    *gd::admit_gpu_virtual_address(0x2000u), *gd::admit_gpu_direct_bytes(4096));
    assert(bridged.has_value());

    auto unknown_root =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), gd::PeerPlacement{},
                                    *gd::admit_gpu_virtual_address(0x2000u), *gd::admit_gpu_direct_bytes(4096));
    assert(!unknown_root.has_value());
    assert(unknown_root.error() == gd::GpuDirectError::PcieRootUnknown);

    auto no_gpu_cap = gpu_caps();
    no_gpu_cap.features.unset(cog::GpuFeature::GpuDirectRdma);
    auto missing_gpu_cap =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), no_gpu_cap, nic_identity(), nic_caps(), same_root(),
                                    *gd::admit_gpu_virtual_address(0x2000u), *gd::admit_gpu_direct_bytes(4096));
    assert(!missing_gpu_cap.has_value());
    assert(missing_gpu_cap.error() == gd::GpuDirectError::MissingGpuRdmaCapability);

    auto no_nic_cap = nic_caps();
    no_nic_cap.features.unset(cog::NicFeature::GpuDirectRdma);
    auto missing_nic_cap =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), no_nic_cap, same_root(),
                                    *gd::admit_gpu_virtual_address(0x2000u), *gd::admit_gpu_direct_bytes(4096));
    assert(!missing_nic_cap.has_value());
    assert(missing_nic_cap.error() == gd::GpuDirectError::MissingNicRdmaCapability);

    auto not_a_nic =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nvme_identity(), nic_caps(), same_root(),
                                    *gd::admit_gpu_virtual_address(0x2000u), *gd::admit_gpu_direct_bytes(4096));
    assert(!not_a_nic.has_value());
    assert(not_a_nic.error() == gd::GpuDirectError::NonNicCog);

    auto no_access = gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(),
                                                 same_root(), *gd::admit_gpu_virtual_address(0x2000u),
                                                 *gd::admit_gpu_direct_bytes(4096), gd::MrAccess{});
    assert(!no_access.has_value());
    assert(no_access.error() == gd::GpuDirectError::InvalidAccess);

    crucible::test::pass("  test_rdma_plan_minting: PASSED\n");
}

void test_registration_boundary() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan = gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), same_root(),
                                            *gd::admit_gpu_virtual_address(0x3000u), *gd::admit_gpu_direct_bytes(4096));
    assert(plan.has_value());

    auto missing_module = gd::register_gpu_memory(*plan);
    assert(!missing_module.has_value());
    assert(missing_module.error() == gd::GpuDirectError::PeerModuleUnavailable);

    auto deferred = gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(),
                                                same_root(), *gd::admit_gpu_virtual_address(0x3000u),
                                                *gd::admit_gpu_direct_bytes(4096), gd::mrc_write_access(), true);
    assert(deferred.has_value());
    auto reg_deferred = gd::register_gpu_memory(*deferred);
    assert(!reg_deferred.has_value());
    assert(reg_deferred.error() == gd::GpuDirectError::RegistrationDeferred);

    auto unavailable = gd::mint_gpu_direct_mr_plan(
        init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), same_root(),
        *gd::admit_gpu_virtual_address(0x3000u), *gd::admit_gpu_direct_bytes(4096), gd::mrc_write_access(), true, true);
    assert(unavailable.has_value());
    auto reg_unavailable = gd::register_gpu_memory(*unavailable);
    assert(!reg_unavailable.has_value());
    assert(reg_unavailable.error() == gd::GpuDirectError::VendorBackendUnavailable);

    crucible::test::pass("  test_registration_boundary: PASSED\n");
}

void test_storage_plan_boundary() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan = gd::mint_gpu_direct_storage_plan(init, gpu_identity(), gpu_caps(), nvme_identity(), same_root(),
                                                 *gd::admit_gpu_virtual_address(0x4000u),
                                                 *gd::admit_gpu_direct_bytes(8192), 128);
    assert(plan.has_value());
    static_assert(std::same_as<std::remove_cvref_t<decltype(*plan)>, gd::DeclaredGpuDirectStoragePlan>);
    assert(plan->value().storage_offset_bytes == 128);

    auto read = gd::read_from_nvme(*plan);
    assert(!read.has_value());
    assert(read.error() == gd::GpuDirectError::StorageBackendUnavailable);
    auto write = gd::write_to_nvme(*plan);
    assert(!write.has_value());
    assert(write.error() == gd::GpuDirectError::StorageBackendUnavailable);

    auto unknown_root =
        gd::mint_gpu_direct_storage_plan(init, gpu_identity(), gpu_caps(), nvme_identity(), gd::PeerPlacement{},
                                         *gd::admit_gpu_virtual_address(0x4000u), *gd::admit_gpu_direct_bytes(8192));
    assert(!unknown_root.has_value());
    assert(unknown_root.error() == gd::GpuDirectError::PcieRootUnknown);

    auto non_nvme =
        gd::mint_gpu_direct_storage_plan(init, gpu_identity(), gpu_caps(), nic_identity(), same_root(),
                                         *gd::admit_gpu_virtual_address(0x4000u), *gd::admit_gpu_direct_bytes(8192));
    assert(!non_nvme.has_value());
    assert(non_nvme.error() == gd::GpuDirectError::NonNvmeCog);

    auto zero_peer = nvme_identity();
    zero_peer.uuid = cog::Uuid{};
    auto undiscovered_peer =
        gd::mint_gpu_direct_storage_plan(init, gpu_identity(), gpu_caps(), zero_peer, same_root(),
                                         *gd::admit_gpu_virtual_address(0x4000u), *gd::admit_gpu_direct_bytes(8192));
    assert(!undiscovered_peer.has_value());
    assert(undiscovered_peer.error() == gd::GpuDirectError::ZeroPeerCog);

    auto no_storage = gpu_caps();
    no_storage.features.unset(cog::GpuFeature::GpuDirectStorage);
    auto missing_storage =
        gd::mint_gpu_direct_storage_plan(init, gpu_identity(), no_storage, nvme_identity(), same_root(),
                                         *gd::admit_gpu_virtual_address(0x4000u), *gd::admit_gpu_direct_bytes(8192));
    assert(!missing_storage.has_value());
    assert(missing_storage.error() == gd::GpuDirectError::MissingGpuStorageCapability);

    crucible::test::pass("  test_storage_plan_boundary: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(gd::GpuVirtualAddress) == sizeof(std::uintptr_t));
    static_assert(sizeof(gd::DeclaredGpuDirectMrPlan) == sizeof(gd::GpuDirectMrPlan));
    static_assert(sizeof(gd::DeclaredGpuDirectStoragePlan) == sizeof(gd::GpuDirectStoragePlan));
    static_assert(std::same_as<gd::DeclaredGpuDirectMrPlan::tag_type, gd::wip_source::GpuDirect>);
    static_assert(gd::CtxFitsGpuDirectMint<::fixy::ColdInitCtx>);
    static_assert(!gd::CtxFitsGpuDirectMint<::fixy::BgDrainCtx>);
    static_assert(!gd::CtxFitsGpuDirectMint<::fixy::HotFgCtx>);
    static_assert(std::is_trivially_copy_constructible_v<gd::GpuDirectMrPlan>);
    static_assert(std::is_trivially_copy_constructible_v<gd::GpuDirectStoragePlan>);
    static_assert(!std::is_default_constructible_v<gd::DeclaredGpuDirectMrPlan>);
    static_assert(!std::is_default_constructible_v<gd::DeclaredGpuDirectStoragePlan>);
    static_assert(!std::is_copy_constructible_v<gd::GpuDirectMrHandle>);
    static_assert(!gd::gpu_direct_backend_implemented,
                  "no driver registers a region, so each registration and storage call is a stub");

    ::fixy::report(::fixy::Sink::Out, "test_cntp_gpu_direct:\n");
    test_admission_and_names();
    test_rdma_plan_minting();
    test_registration_boundary();
    test_storage_plan_boundary();
    crucible::test::pass("test_cntp_gpu_direct: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
