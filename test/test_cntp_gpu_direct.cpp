#include <crucible/cntp/_wip/GpuDirect.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <concepts>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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

// The error of a registration.  A region is linear, so the region of a
// registration that succeeds ends here, and the error then holds no value.
[[nodiscard]] ::fixy::Option<gd::GpuDirectError>
registration_error(::fixy::Result<gd::OwnedGpuDirectMr, gd::GpuDirectError> registered) {
    return std::move(registered)
        .match(
            [](gd::OwnedGpuDirectMr region) noexcept {
                drop(std::move(region));
                return ::fixy::Option<gd::GpuDirectError>{::fixy::none};
            },
            [](gd::GpuDirectError error) noexcept { return ::fixy::some(error); });
}

void test_admission_and_names() {
    CRUCIBLE_TEST_ASSERT(kTest, gd::gpu_direct_error_name(gd::GpuDirectError::PcieRootMismatch) == "PcieRootMismatch");
    CRUCIBLE_TEST_ASSERT(kTest, gd::mr_access_flag_name(gd::MrAccessFlag::RemoteWrite) == "RemoteWrite");

    auto address = gd::admit_gpu_virtual_address(0x1000u);
    CRUCIBLE_TEST_ASSERT(kTest, address.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(address).expect("the address is not null").value() == 0x1000u);

    auto null_address = gd::admit_gpu_virtual_address(std::uintptr_t{0});
    CRUCIBLE_TEST_ASSERT(kTest, null_address.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, null_address.err() == ::fixy::some(gd::GpuDirectError::NullGpuAddress));

    auto bytes = gd::admit_gpu_direct_bytes(4096);
    CRUCIBLE_TEST_ASSERT(kTest, bytes.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(bytes).expect("4096 bytes is positive").value() == 4096);

    auto zero_bytes = gd::admit_gpu_direct_bytes(0);
    CRUCIBLE_TEST_ASSERT(kTest, zero_bytes.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, zero_bytes.err() == ::fixy::some(gd::GpuDirectError::InvalidByteCount));

    CRUCIBLE_TEST_ASSERT(kTest, gd::mrc_write_access().test(gd::MrAccessFlag::LocalWrite));
    CRUCIBLE_TEST_ASSERT(kTest, gd::mrc_write_access().test(gd::MrAccessFlag::RemoteWrite));
    CRUCIBLE_TEST_ASSERT(kTest, !gd::placement_known(gd::PeerPlacement{}));

    ::fixy::check::pass(kTest, "  test_admission_and_names: PASSED\n");
}

void test_rdma_plan_minting() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan = gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), same_root(),
                                            gd::admit_gpu_virtual_address(0x2000u).expect("the address is not null"),
                                            gd::admit_gpu_direct_bytes(1u << 20u).expect("the byte count is positive"))
                    .expect("a GPU and a NIC on one PCIe root make a plan");
    static_assert(std::same_as<decltype(plan), gd::DeclaredGpuDirectMrPlan>);
    CRUCIBLE_TEST_ASSERT(kTest, plan.value().gpu.uuid == gpu_identity().uuid);
    CRUCIBLE_TEST_ASSERT(kTest, plan.value().nic.uuid == nic_identity().uuid);
    CRUCIBLE_TEST_ASSERT(kTest, plan.value().bytes.value() == (1u << 20u));

    gd::PeerPlacement remote_root = same_root();
    remote_root.peer_pcie_root = pcie_root(8);
    auto mismatch =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), remote_root,
                                    gd::admit_gpu_virtual_address(0x2000u).expect("the address is not null"),
                                    gd::admit_gpu_direct_bytes(4096).expect("the byte count is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, mismatch.err() == ::fixy::some(gd::GpuDirectError::PcieRootMismatch));

    remote_root.peer_bridge_present = true;
    auto bridged =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), remote_root,
                                    gd::admit_gpu_virtual_address(0x2000u).expect("the address is not null"),
                                    gd::admit_gpu_direct_bytes(4096).expect("the byte count is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, bridged.is_ok());

    auto unknown_root =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), gd::PeerPlacement{},
                                    gd::admit_gpu_virtual_address(0x2000u).expect("the address is not null"),
                                    gd::admit_gpu_direct_bytes(4096).expect("the byte count is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, unknown_root.err() == ::fixy::some(gd::GpuDirectError::PcieRootUnknown));

    auto no_gpu_cap = gpu_caps();
    no_gpu_cap.features.unset(cog::GpuFeature::GpuDirectRdma);
    auto missing_gpu_cap =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), no_gpu_cap, nic_identity(), nic_caps(), same_root(),
                                    gd::admit_gpu_virtual_address(0x2000u).expect("the address is not null"),
                                    gd::admit_gpu_direct_bytes(4096).expect("the byte count is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, missing_gpu_cap.err() == ::fixy::some(gd::GpuDirectError::MissingGpuRdmaCapability));

    auto no_nic_cap = nic_caps();
    no_nic_cap.features.unset(cog::NicFeature::GpuDirectRdma);
    auto missing_nic_cap =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), no_nic_cap, same_root(),
                                    gd::admit_gpu_virtual_address(0x2000u).expect("the address is not null"),
                                    gd::admit_gpu_direct_bytes(4096).expect("the byte count is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, missing_nic_cap.err() == ::fixy::some(gd::GpuDirectError::MissingNicRdmaCapability));

    auto not_a_nic =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nvme_identity(), nic_caps(), same_root(),
                                    gd::admit_gpu_virtual_address(0x2000u).expect("the address is not null"),
                                    gd::admit_gpu_direct_bytes(4096).expect("the byte count is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, not_a_nic.err() == ::fixy::some(gd::GpuDirectError::NonNicCog));

    auto no_access = gd::mint_gpu_direct_mr_plan(
        init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), same_root(),
        gd::admit_gpu_virtual_address(0x2000u).expect("the address is not null"),
        gd::admit_gpu_direct_bytes(4096).expect("the byte count is positive"), gd::MrAccess{});
    CRUCIBLE_TEST_ASSERT(kTest, no_access.err() == ::fixy::some(gd::GpuDirectError::InvalidAccess));

    ::fixy::check::pass(kTest, "  test_rdma_plan_minting: PASSED\n");
}

void test_registration_boundary() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan = gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), same_root(),
                                            gd::admit_gpu_virtual_address(0x3000u).expect("the address is not null"),
                                            gd::admit_gpu_direct_bytes(4096).expect("the byte count is positive"))
                    .expect("a GPU and a NIC on one PCIe root make a plan");

    CRUCIBLE_TEST_ASSERT(kTest, registration_error(gd::register_gpu_memory(plan))
                                    == ::fixy::some(gd::GpuDirectError::PeerModuleUnavailable));

    auto deferred =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), same_root(),
                                    gd::admit_gpu_virtual_address(0x3000u).expect("the address is not null"),
                                    gd::admit_gpu_direct_bytes(4096).expect("the byte count is positive"),
                                    gd::mrc_write_access(), true)
            .expect("a plan with the peer module makes a plan");
    CRUCIBLE_TEST_ASSERT(kTest, registration_error(gd::register_gpu_memory(deferred))
                                    == ::fixy::some(gd::GpuDirectError::RegistrationDeferred));

    auto unavailable =
        gd::mint_gpu_direct_mr_plan(init, gpu_identity(), gpu_caps(), nic_identity(), nic_caps(), same_root(),
                                    gd::admit_gpu_virtual_address(0x3000u).expect("the address is not null"),
                                    gd::admit_gpu_direct_bytes(4096).expect("the byte count is positive"),
                                    gd::mrc_write_access(), true, true)
            .expect("a plan that asks for the backend makes a plan");
    CRUCIBLE_TEST_ASSERT(kTest, registration_error(gd::register_gpu_memory(unavailable))
                                    == ::fixy::some(gd::GpuDirectError::VendorBackendUnavailable));

    ::fixy::check::pass(kTest, "  test_registration_boundary: PASSED\n");
}

void test_storage_plan_boundary() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan =
        gd::mint_gpu_direct_storage_plan(init, gpu_identity(), gpu_caps(), nvme_identity(), same_root(),
                                         gd::admit_gpu_virtual_address(0x4000u).expect("the address is not null"),
                                         gd::admit_gpu_direct_bytes(8192).expect("the byte count is positive"), 128)
            .expect("a GPU and an NVMe drive on one PCIe root make a plan");
    static_assert(std::same_as<decltype(plan), gd::DeclaredGpuDirectStoragePlan>);
    CRUCIBLE_TEST_ASSERT(kTest, plan.value().storage_offset_bytes == 128);

    CRUCIBLE_TEST_ASSERT(kTest,
                         gd::read_from_nvme(plan).err() == ::fixy::some(gd::GpuDirectError::StorageBackendUnavailable));
    CRUCIBLE_TEST_ASSERT(kTest,
                         gd::write_to_nvme(plan).err() == ::fixy::some(gd::GpuDirectError::StorageBackendUnavailable));

    auto unknown_root =
        gd::mint_gpu_direct_storage_plan(init, gpu_identity(), gpu_caps(), nvme_identity(), gd::PeerPlacement{},
                                         gd::admit_gpu_virtual_address(0x4000u).expect("the address is not null"),
                                         gd::admit_gpu_direct_bytes(8192).expect("the byte count is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, unknown_root.err() == ::fixy::some(gd::GpuDirectError::PcieRootUnknown));

    auto non_nvme =
        gd::mint_gpu_direct_storage_plan(init, gpu_identity(), gpu_caps(), nic_identity(), same_root(),
                                         gd::admit_gpu_virtual_address(0x4000u).expect("the address is not null"),
                                         gd::admit_gpu_direct_bytes(8192).expect("the byte count is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, non_nvme.err() == ::fixy::some(gd::GpuDirectError::NonNvmeCog));

    auto zero_peer = nvme_identity();
    zero_peer.uuid = cog::Uuid{};
    auto undiscovered_peer =
        gd::mint_gpu_direct_storage_plan(init, gpu_identity(), gpu_caps(), zero_peer, same_root(),
                                         gd::admit_gpu_virtual_address(0x4000u).expect("the address is not null"),
                                         gd::admit_gpu_direct_bytes(8192).expect("the byte count is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, undiscovered_peer.err() == ::fixy::some(gd::GpuDirectError::ZeroPeerCog));

    auto no_storage = gpu_caps();
    no_storage.features.unset(cog::GpuFeature::GpuDirectStorage);
    auto missing_storage =
        gd::mint_gpu_direct_storage_plan(init, gpu_identity(), no_storage, nvme_identity(), same_root(),
                                         gd::admit_gpu_virtual_address(0x4000u).expect("the address is not null"),
                                         gd::admit_gpu_direct_bytes(8192).expect("the byte count is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, missing_storage.err() == ::fixy::some(gd::GpuDirectError::MissingGpuStorageCapability));

    ::fixy::check::pass(kTest, "  test_storage_plan_boundary: PASSED\n");
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
    ::fixy::check::pass(kTest, "test_cntp_gpu_direct: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
