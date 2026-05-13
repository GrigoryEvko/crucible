#include <crucible/cog/SrIov.h>

#include <array>
#include <cassert>
#include <concepts>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace eff = crucible::effects;
namespace saf = crucible::safety;
namespace sriov = crucible::cog::sriov;
namespace cntp = crucible::cntp;

namespace {

cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x147, 0x51'10};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

cog::NicPortTargetCaps sriov_caps() {
    cog::NicPortTargetCaps caps{};
    caps.features.set(cog::NicFeature::SrIov);
    caps.max_tx_queues = saf::Tagged<std::uint16_t, saf::source::Vendor>{64};
    caps.max_rx_queues = saf::Tagged<std::uint16_t, saf::source::Vendor>{64};
    caps.max_qp_count = saf::Tagged<std::uint32_t, saf::source::Vendor>{4096};
    caps.max_mr_count = saf::Tagged<std::uint32_t, saf::source::Vendor>{4096};
    return caps;
}

cntp::NicInterfaceName iface() {
    auto name = cntp::NicInterfaceName::from("ens7f0");
    assert(name.has_value());
    return *name;
}

void test_admission() {
    auto count = sriov::admit_vf_count(8);
    assert(count.has_value());
    assert(count->value() == 8);

    auto zero_count = sriov::admit_vf_count(0);
    assert(!zero_count.has_value());
    assert(zero_count.error() == sriov::SrIovError::InvalidVfCount);

    auto vlan = sriov::admit_vlan(4094);
    assert(vlan.has_value());
    assert(vlan->value() == 4094);

    auto bad_vlan = sriov::admit_vlan(4095);
    assert(!bad_vlan.has_value());
    assert(bad_vlan.error() == sriov::SrIovError::InvalidVlan);

    auto mac = sriov::admit_mac(sriov::MacAddress::locally_administered(7));
    assert(mac.has_value());
    assert(mac->value().bytes[5] == 7);

    auto multicast = sriov::admit_mac(sriov::MacAddress{{0x01, 0, 0, 0, 0, 1}});
    assert(!multicast.has_value());
    assert(multicast.error() == sriov::SrIovError::InvalidMac);

    std::printf("  test_admission: PASSED\n");
}

void test_mint_and_handles() {
    sriov::VfConfig config{};
    config.mac = *sriov::admit_mac(sriov::MacAddress::locally_administered(9));
    config.vlan = *sriov::admit_vlan(42);
    config.rate_limit_mbps = *sriov::admit_rate_limit_mbps(100'000);
    config.max_qps = *sriov::admit_resource_limit(1024);
    config.max_mrs = *sriov::admit_resource_limit(2048);

    auto plan = sriov::mint_sriov_plan(
        eff::ColdInitCtx{}, nic_identity(), sriov_caps(), iface(),
        *sriov::admit_vf_count(4), config);
    assert(plan.has_value());
    static_assert(std::same_as<
                  std::remove_cvref_t<decltype(*plan)>,
                  sriov::DeclaredSrIovPlan>);
    assert(plan->value().num_vfs.value() == 4);
    assert(plan->value().default_vf.vlan.value() == 42);

    std::array<sriov::VfHandle, 4> handles{};
    auto materialized =
        sriov::materialize_vf_handles(*plan, std::span<sriov::VfHandle>{handles});
    assert(materialized.has_value());
    assert(materialized->size() == 4);
    assert((*materialized)[0].parent_uuid == nic_identity().uuid);
    assert((*materialized)[0].identity.kind == cog::CogKind::NicPort);
    assert((*materialized)[0].identity.uuid != (*materialized)[1].identity.uuid);

    auto second = sriov::vf_handle_at(
        *plan, sriov::VfIndex{std::uint16_t{1}, typename sriov::VfIndex::Trusted{}});
    assert(second.has_value());
    assert(second->index.value() == 1);

    auto out_of_range = sriov::vf_handle_at(
        *plan, sriov::VfIndex{std::uint16_t{4}, typename sriov::VfIndex::Trusted{}});
    assert(!out_of_range.has_value());
    assert(out_of_range.error() == sriov::SrIovError::VfIndexOutOfRange);

    std::array<sriov::VfHandle, 2> small{};
    auto too_small =
        sriov::materialize_vf_handles(*plan, std::span<sriov::VfHandle>{small});
    assert(!too_small.has_value());
    assert(too_small.error() == sriov::SrIovError::InsufficientHandleCapacity);

    std::printf("  test_mint_and_handles: PASSED\n");
}

void test_identity_and_capability_gates() {
    auto zero = sriov::mint_sriov_plan(
        eff::ColdInitCtx{}, cog::CogIdentity{}, sriov_caps(), iface(),
        *sriov::admit_vf_count(1));
    assert(!zero.has_value());
    assert(zero.error() == sriov::SrIovError::ZeroCog);

    auto gpu = nic_identity();
    gpu.kind = cog::CogKind::Gpu;
    auto wrong_kind = sriov::mint_sriov_plan(
        eff::ColdInitCtx{}, gpu, sriov_caps(), iface(),
        *sriov::admit_vf_count(1));
    assert(!wrong_kind.has_value());
    assert(wrong_kind.error() == sriov::SrIovError::NonNicCog);

    auto caps = sriov_caps();
    caps.features.unset(cog::NicFeature::SrIov);
    auto missing_cap = sriov::mint_sriov_plan(
        eff::ColdInitCtx{}, nic_identity(), caps, iface(),
        *sriov::admit_vf_count(1));
    assert(!missing_cap.has_value());
    assert(missing_cap.error() == sriov::SrIovError::MissingSrIovCapability);

    auto query = sriov::query_current(nic_identity(), iface());
    assert(!query.has_value());
    assert(query.error() == sriov::SrIovError::QueryDeferred);

    std::printf("  test_identity_and_capability_gates: PASSED\n");
}

void test_privileged_boundaries() {
    auto plan = sriov::mint_sriov_plan(
        eff::ColdInitCtx{}, nic_identity(), sriov_caps(), iface(),
        *sriov::admit_vf_count(2));
    assert(plan.has_value());

    std::array<sriov::VfHandle, 2> handles{};
    auto enable = sriov::enable(*plan, std::span<sriov::VfHandle>{handles});
    assert(!enable.has_value());
    assert(enable.error() == sriov::SrIovError::PrivilegedApplyDeferred);

    auto privileged = sriov::mint_sriov_plan(
        eff::ColdInitCtx{}, nic_identity(), sriov_caps(), iface(),
        *sriov::admit_vf_count(2), {}, true);
    assert(privileged.has_value());
    auto privileged_enable =
        sriov::enable(*privileged, std::span<sriov::VfHandle>{handles});
    assert(!privileged_enable.has_value());
    assert(privileged_enable.error()
           == sriov::SrIovError::PrivilegedBackendUnavailable);

    auto handle = sriov::make_vf_handle(
        nic_identity(), sriov::VfIndex{std::uint16_t{0},
                                       typename sriov::VfIndex::Trusted{}});
    auto cfg = sriov::declare_vf_config(sriov::VfConfig{});
    auto configure = sriov::configure_vf(handle, cfg);
    assert(!configure.has_value());
    assert(configure.error() == sriov::SrIovError::PrivilegedApplyDeferred);

    assert(sriov::sriov_error_name(sriov::SrIovError::QueryDeferred)
           == std::string_view{"QueryDeferred"});

    std::printf("  test_privileged_boundaries: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(sriov::VfMacAddress) == sizeof(sriov::MacAddress));
    static_assert(sizeof(sriov::DeclaredSrIovPlan) == sizeof(sriov::SrIovPlan));
    static_assert(std::same_as<
                  sriov::DeclaredSrIovPlan::tag_type,
                  saf::source::SrIov>);
    static_assert(sriov::CtxFitsSrIovMint<eff::ColdInitCtx>);
    static_assert(!sriov::CtxFitsSrIovMint<eff::BgDrainCtx>);
    static_assert(std::is_trivially_copyable_v<sriov::VfConfig>);
    static_assert(std::is_trivially_copyable_v<sriov::SrIovPlan>);

    std::printf("test_sriov:\n");
    test_admission();
    test_mint_and_handles();
    test_identity_and_capability_gates();
    test_privileged_boundaries();
    std::printf("test_sriov: all PASSED\n");
    return 0;
}
