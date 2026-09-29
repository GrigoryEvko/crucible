#include <crucible/cog/SrIov.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include <array>
#include <cassert>
#include <concepts>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

// Every sriov::enable, configure_vf, disable and query_current entry point
// carries [[deprecated("CRUCIBLE_STUB:...")]] while no CAP_NET_ADMIN backend
// exists. Suppressing the diagnostic here is what lets the stubs be tested.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace cog = crucible::cog;
namespace eff = ::fixy;
namespace sriov = crucible::cog::sriov;
namespace cntp = crucible::cntp;

namespace {

eff::ColdInitCtx init_ctx() { return eff::ColdInitCtx{::foundation::effects::testing::init()}; }

cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x147, 0x5110};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

cog::NicPortTargetCaps sriov_caps() {
    cog::NicPortTargetCaps caps{};
    caps.features.set(cog::NicFeature::SrIov);
    caps.max_tx_queues = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint16_t>(64);
    caps.max_rx_queues = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint16_t>(64);
    caps.max_qp_count = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint32_t>(4096);
    caps.max_mr_count = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint32_t>(4096);
    return caps;
}

cntp::NicInterfaceName iface() {
    auto name = cntp::NicInterfaceName::from("ens7f0");
    assert(name.has_value());
    return *name;
}

sriov::DeclaredSrIovPlan two_vf_plan(bool allow_privileged_apply = false) {
    auto plan = sriov::mint_sriov_plan(init_ctx(), nic_identity(), sriov_caps(), iface(), *sriov::admit_vf_count(2), {},
                                       allow_privileged_apply);
    assert(plan.has_value());
    return *plan;
}

void test_admission() {
    auto count = sriov::admit_vf_count(8);
    assert(count.has_value());
    assert(count->value() == 8);

    auto zero_count = sriov::admit_vf_count(0);
    assert(!zero_count.has_value());
    assert(zero_count.error() == sriov::SrIovError::InvalidVfCount);

    auto index = sriov::admit_vf_index(4095);
    assert(index.has_value());
    auto bad_index = sriov::admit_vf_index(4096);
    assert(!bad_index.has_value());
    assert(bad_index.error() == sriov::SrIovError::InvalidVfIndex);

    auto vlan = sriov::admit_vlan(4094);
    assert(vlan.has_value());
    assert(vlan->value() == 4094);

    auto bad_vlan = sriov::admit_vlan(4095);
    assert(!bad_vlan.has_value());
    assert(bad_vlan.error() == sriov::SrIovError::InvalidVlan);

    auto bad_rate = sriov::admit_rate_limit_mbps(1000000001ull);
    assert(!bad_rate.has_value());
    assert(bad_rate.error() == sriov::SrIovError::InvalidRateLimit);

    auto bad_limit = sriov::admit_resource_limit(1000001);
    assert(!bad_limit.has_value());
    assert(bad_limit.error() == sriov::SrIovError::InvalidResourceLimit);

    auto mac = sriov::admit_mac(sriov::MacAddress::locally_administered(7));
    assert(mac.has_value());
    assert(mac->value().bytes[5] == 7);

    auto multicast = sriov::admit_mac(sriov::MacAddress{{0x01, 0, 0, 0, 0, 1}});
    assert(!multicast.has_value());
    assert(multicast.error() == sriov::SrIovError::InvalidMac);

    auto zero_mac = sriov::admit_mac(sriov::MacAddress{});
    assert(!zero_mac.has_value());
    assert(zero_mac.error() == sriov::SrIovError::InvalidMac);

    std::printf("  test_admission: PASSED\n");
}

void test_mint_and_handles() {
    sriov::VfConfig config{};
    config.mac = *sriov::admit_mac(sriov::MacAddress::locally_administered(9));
    config.vlan = *sriov::admit_vlan(42);
    config.rate_limit_mbps = *sriov::admit_rate_limit_mbps(100000);
    config.max_qps = *sriov::admit_resource_limit(1024);
    config.max_mrs = *sriov::admit_resource_limit(2048);

    auto plan =
        sriov::mint_sriov_plan(init_ctx(), nic_identity(), sriov_caps(), iface(), *sriov::admit_vf_count(4), config);
    assert(plan.has_value());
    static_assert(std::same_as<std::remove_cvref_t<decltype(*plan)>, sriov::DeclaredSrIovPlan>);
    assert(plan->value().num_vfs.value() == 4);
    assert(plan->value().default_vf.vlan.value() == 42);

    std::array<sriov::VfHandle, 4> handles{};
    auto materialized = sriov::materialize_vf_handles(*plan, std::span<sriov::VfHandle>{handles});
    assert(materialized.has_value());
    assert(materialized->size() == 4);
    assert((*materialized)[0].parent_uuid() == nic_identity().uuid);
    assert((*materialized)[0].identity().kind == cog::CogKind::NicPort);
    assert((*materialized)[0].identity().uuid != (*materialized)[1].identity().uuid);
    assert((*materialized)[3].index().value() == 3);

    auto second = sriov::vf_handle_at(*plan, *sriov::admit_vf_index(1));
    assert(second.has_value());
    assert(second->index().value() == 1);
    assert(second->identity().uuid == (*materialized)[1].identity().uuid);

    auto out_of_range = sriov::vf_handle_at(*plan, *sriov::admit_vf_index(4));
    assert(!out_of_range.has_value());
    assert(out_of_range.error() == sriov::SrIovError::VfIndexOutOfRange);

    std::array<sriov::VfHandle, 2> small{};
    auto too_small = sriov::materialize_vf_handles(*plan, std::span<sriov::VfHandle>{small});
    assert(!too_small.has_value());
    assert(too_small.error() == sriov::SrIovError::InsufficientHandleCapacity);

    // An empty slot names no function.
    sriov::VfHandle empty{};
    assert(empty.parent_uuid().is_zero());
    assert(empty.identity().uuid.is_zero());

    std::printf("  test_mint_and_handles: PASSED\n");
}

void test_identity_and_capability_gates() {
    auto zero =
        sriov::mint_sriov_plan(init_ctx(), cog::CogIdentity{}, sriov_caps(), iface(), *sriov::admit_vf_count(1));
    assert(!zero.has_value());
    assert(zero.error() == sriov::SrIovError::ZeroCog);

    auto gpu = nic_identity();
    gpu.kind = cog::CogKind::Gpu;
    auto wrong_kind = sriov::mint_sriov_plan(init_ctx(), gpu, sriov_caps(), iface(), *sriov::admit_vf_count(1));
    assert(!wrong_kind.has_value());
    assert(wrong_kind.error() == sriov::SrIovError::NonNicCog);

    auto caps = sriov_caps();
    caps.features.unset(cog::NicFeature::SrIov);
    auto missing_cap = sriov::mint_sriov_plan(init_ctx(), nic_identity(), caps, iface(), *sriov::admit_vf_count(1));
    assert(!missing_cap.has_value());
    assert(missing_cap.error() == sriov::SrIovError::MissingSrIovCapability);

    auto no_name = sriov::mint_sriov_plan(init_ctx(), nic_identity(), sriov_caps(), cntp::NicInterfaceName{},
                                          *sriov::admit_vf_count(1));
    assert(!no_name.has_value());
    assert(no_name.error() == sriov::SrIovError::InvalidInterfaceName);

    auto query = sriov::query_current(nic_identity(), iface());
    assert(!query.has_value());
    assert(query.error() == sriov::SrIovError::QueryDeferred);

    std::printf("  test_identity_and_capability_gates: PASSED\n");
}

void test_privileged_boundaries() {
    auto plan = two_vf_plan();

    std::array<sriov::VfHandle, 2> handles{};
    auto enable = sriov::enable(plan, std::span<sriov::VfHandle>{handles});
    assert(!enable.has_value());
    assert(enable.error() == sriov::SrIovError::PrivilegedApplyDeferred);

    auto privileged = two_vf_plan(true);
    auto privileged_enable = sriov::enable(privileged, std::span<sriov::VfHandle>{handles});
    assert(!privileged_enable.has_value());
    assert(privileged_enable.error() == sriov::SrIovError::PrivilegedBackendUnavailable);

    auto handle = sriov::vf_handle_at(plan, sriov::first_vf_index);
    assert(handle.has_value());
    auto cfg = sriov::declare_vf_config(sriov::VfConfig{});
    auto configure = sriov::configure_vf(*handle, cfg);
    assert(!configure.has_value());
    assert(configure.error() == sriov::SrIovError::PrivilegedApplyDeferred);

    auto empty_configure = sriov::configure_vf(sriov::VfHandle{}, cfg);
    assert(!empty_configure.has_value());
    assert(empty_configure.error() == sriov::SrIovError::ZeroCog);

    auto disable = sriov::disable(plan);
    assert(!disable.has_value());
    assert(disable.error() == sriov::SrIovError::PrivilegedApplyDeferred);

    std::printf("  test_privileged_boundaries: PASSED\n");
}

// The distinction these assertions defend is between a substrate that reports
// "not attempted" and one that silently admits the request and does nothing.
// Each privileged surface must name which of the two it is.
void test_apply_paths_are_stubbed() {
    static_assert(sriov::privileged_apply_implemented == false,
                  "the SR-IOV apply paths are stubs. privileged_apply_implemented "
                  "turns true only alongside a CAP_NET_ADMIN backend and live-NIC "
                  "fixtures in place of this one.");
    static_assert(std::is_same_v<decltype(sriov::privileged_apply_implemented), const bool>,
                  "the honesty trait must be a compile-time bool");

    auto plan = two_vf_plan();

    std::array<sriov::VfHandle, 2> handles{};
    auto deferred = sriov::enable(plan, std::span<sriov::VfHandle>{handles});
    assert(!deferred.has_value());
    assert(deferred.error() == sriov::SrIovError::PrivilegedApplyDeferred);

    auto requested = two_vf_plan(true);
    auto requested_enable = sriov::enable(requested, std::span<sriov::VfHandle>{handles});
    assert(!requested_enable.has_value());
    assert(requested_enable.error() == sriov::SrIovError::PrivilegedBackendUnavailable);

    auto requested_disable = sriov::disable(requested);
    assert(!requested_disable.has_value());
    assert(requested_disable.error() == sriov::SrIovError::PrivilegedBackendUnavailable);

    auto handle = sriov::vf_handle_at(plan, sriov::first_vf_index);
    assert(handle.has_value());
    auto cfg = sriov::declare_vf_config(sriov::VfConfig{});
    auto configure = sriov::configure_vf(*handle, cfg);
    assert(!configure.has_value());
    assert(configure.error() == sriov::SrIovError::PrivilegedApplyDeferred);

    auto query = sriov::query_current(nic_identity(), iface());
    assert(!query.has_value());
    assert(query.error() == sriov::SrIovError::QueryDeferred);

    std::printf("  test_apply_paths_are_stubbed: PASSED\n");
}

void test_enumerator_names() {
    using ::foundation::reflect::enum_name;
    assert(enum_name(sriov::SrIovError::QueryDeferred) == std::string_view{"QueryDeferred"});
    assert(enum_name(static_cast<sriov::SrIovError>(0xFF)) == std::string_view{"<unknown SrIovError>"});

    std::printf("  test_enumerator_names: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(sriov::VfMacAddress) == sizeof(sriov::MacAddress));
    static_assert(sizeof(sriov::DeclaredSrIovPlan) == sizeof(sriov::SrIovPlan));
    static_assert(std::same_as<sriov::DeclaredSrIovPlan::tag_type, ::fixy::tags::source::SrIov>);
    static_assert(sriov::CtxFitsSrIovMint<eff::ColdInitCtx>);
    static_assert(!sriov::CtxFitsSrIovMint<eff::BgDrainCtx>);
    static_assert(!sriov::CtxFitsSrIovMint<eff::HotFgCtx>);
    static_assert(!sriov::CtxFitsSrIovMint<int>);
    static_assert(!std::is_constructible_v<sriov::VfCount, std::uint16_t>,
                  "a count is reached only through its mint or its admission");
    static_assert(!std::is_constructible_v<sriov::DeclaredSrIovPlan, sriov::SrIovPlan>,
                  "a declared plan is reached only through a mint");
    static_assert(!std::is_constructible_v<sriov::VfHandle, cog::CogIdentity, sriov::VfIndex>,
                  "a handle is built only from a declared plan");
    static_assert(std::is_trivially_copy_constructible_v<sriov::VfConfig>);
    static_assert(std::is_trivially_copy_constructible_v<sriov::SrIovPlan>);

    static_assert(!sriov::privileged_apply_implemented,
                  "the SR-IOV substrate is a stub. Every enable and configure path "
                  "returns PrivilegedApplyDeferred or PrivilegedBackendUnavailable, "
                  "and query_current returns QueryDeferred. The trait turns true "
                  "only alongside a CAP_NET_ADMIN backend and live-NIC fixtures.");

    std::printf("test_sriov:\n");
    test_admission();
    test_mint_and_handles();
    test_identity_and_capability_gates();
    test_privileged_boundaries();
    test_apply_paths_are_stubbed();
    test_enumerator_names();
    std::printf("test_sriov: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
