#include <crucible/cog/SrIov.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include "test_assert.h"
#include <concepts>
#include <cstdio>
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

cntp::NicInterfaceName iface() { return cntp::NicInterfaceName::from("ens7f0").expect("ens7f0 is an interface name"); }

sriov::DeclaredSrIovPlan two_vf_plan(bool allow_privileged_apply = false) {
    return sriov::mint_sriov_plan(init_ctx(), nic_identity(), sriov_caps(), iface(),
                                  sriov::admit_vf_count(2).expect("2 functions fit"), {}, allow_privileged_apply)
        .expect("a NicPort with SR-IOV and a named interface make a plan");
}

void test_admission() {
    auto count = sriov::admit_vf_count(8);
    assert(count.is_ok());
    assert(std::move(count).expect("8 functions fit").value() == 8);

    auto zero_count = sriov::admit_vf_count(0);
    assert(zero_count.is_err());
    assert(zero_count.err() == ::fixy::some(sriov::SrIovError::InvalidVfCount));

    auto index = sriov::admit_vf_index(4095);
    assert(index.is_ok());
    auto bad_index = sriov::admit_vf_index(4096);
    assert(bad_index.is_err());
    assert(bad_index.err() == ::fixy::some(sriov::SrIovError::InvalidVfIndex));

    auto vlan = sriov::admit_vlan(4094);
    assert(vlan.is_ok());
    assert(std::move(vlan).expect("4094 is a VLAN id").value() == 4094);

    auto bad_vlan = sriov::admit_vlan(4095);
    assert(bad_vlan.is_err());
    assert(bad_vlan.err() == ::fixy::some(sriov::SrIovError::InvalidVlan));

    auto bad_rate = sriov::admit_rate_limit_mbps(1000000001ull);
    assert(bad_rate.is_err());
    assert(bad_rate.err() == ::fixy::some(sriov::SrIovError::InvalidRateLimit));

    auto bad_limit = sriov::admit_resource_limit(1000001);
    assert(bad_limit.is_err());
    assert(bad_limit.err() == ::fixy::some(sriov::SrIovError::InvalidResourceLimit));

    auto mac = sriov::admit_mac(sriov::MacAddress::locally_administered(7));
    assert(mac.is_ok());
    assert(std::move(mac).expect("the address is unicast").value().bytes.at(::fixy::index<5>()) == 7);

    auto multicast = sriov::admit_mac(sriov::MacAddress{{0x01, 0, 0, 0, 0, 1}});
    assert(multicast.is_err());
    assert(multicast.err() == ::fixy::some(sriov::SrIovError::InvalidMac));

    auto zero_mac = sriov::admit_mac(sriov::MacAddress{});
    assert(zero_mac.is_err());
    assert(zero_mac.err() == ::fixy::some(sriov::SrIovError::InvalidMac));

    crucible::test::pass("  test_admission: PASSED\n");
}

void test_mint_and_handles() {
    sriov::VfConfig config{};
    config.mac = sriov::admit_mac(sriov::MacAddress::locally_administered(9)).expect("the address is unicast");
    config.vlan = sriov::admit_vlan(42).expect("42 is a VLAN id");
    config.rate_limit_mbps = sriov::admit_rate_limit_mbps(100000).expect("100 Gbps fits");
    config.max_qps = sriov::admit_resource_limit(1024).expect("1024 pairs fit");
    config.max_mrs = sriov::admit_resource_limit(2048).expect("2048 regions fit");

    auto plan = sriov::mint_sriov_plan(init_ctx(), nic_identity(), sriov_caps(), iface(),
                                       sriov::admit_vf_count(4).expect("4 functions fit"), config)
                    .expect("a NicPort with SR-IOV and a named interface make a plan");
    static_assert(std::same_as<decltype(plan), sriov::DeclaredSrIovPlan>);
    assert(plan.value().num_vfs.value() == 4);
    assert(plan.value().default_vf.vlan.value() == 42);

    // Six slots hold the four handles, so the View of the handles is the
    // first four slots.
    ::foundation::core::FixedArray<sriov::VfHandle, 6> handles{};
    auto materialized = sriov::materialize_vf_handles(plan, handles.view()).expect("six slots hold four handles");
    assert(materialized.size() == 4);
    sriov::VfHandle const& first = handles.at(::fixy::index<0>());
    sriov::VfHandle const& second_slot = handles.at(::fixy::index<1>());
    assert(first.parent_uuid() == nic_identity().uuid);
    assert(first.identity().kind == cog::CogKind::NicPort);
    assert(first.identity().uuid != second_slot.identity().uuid);
    assert(handles.at(::fixy::index<3>()).index().value() == 3);
    assert(handles.at(::fixy::index<4>()).identity().uuid.is_zero());
    std::uint16_t expected_index = 0;
    for (sriov::VfHandle const& handle : materialized) {
        assert(handle.index().value() == expected_index);
        ++expected_index;
    }

    auto second = sriov::vf_handle_at(plan, sriov::admit_vf_index(1).expect("1 is a function index"))
                      .expect("index 1 is below the count of four");
    assert(second.index().value() == 1);
    assert(second.identity().uuid == second_slot.identity().uuid);

    auto out_of_range = sriov::vf_handle_at(plan, sriov::admit_vf_index(4).expect("4 is a function index"));
    assert(out_of_range.err() == ::fixy::some(sriov::SrIovError::VfIndexOutOfRange));

    ::foundation::core::FixedArray<sriov::VfHandle, 2> small{};
    auto too_small = sriov::materialize_vf_handles(plan, small.view());
    assert(too_small.err() == ::fixy::some(sriov::SrIovError::InsufficientHandleCapacity));

    // An empty slot names no function.
    sriov::VfHandle empty{};
    assert(empty.parent_uuid().is_zero());
    assert(empty.identity().uuid.is_zero());

    crucible::test::pass("  test_mint_and_handles: PASSED\n");
}

void test_identity_and_capability_gates() {
    auto zero = sriov::mint_sriov_plan(init_ctx(), cog::CogIdentity{}, sriov_caps(), iface(),
                                       sriov::admit_vf_count(1).expect("1 function fits"));
    assert(zero.err() == ::fixy::some(sriov::SrIovError::ZeroCog));

    auto gpu = nic_identity();
    gpu.kind = cog::CogKind::Gpu;
    auto wrong_kind = sriov::mint_sriov_plan(init_ctx(), gpu, sriov_caps(), iface(),
                                             sriov::admit_vf_count(1).expect("1 function fits"));
    assert(wrong_kind.err() == ::fixy::some(sriov::SrIovError::NonNicCog));

    auto caps = sriov_caps();
    caps.features.unset(cog::NicFeature::SrIov);
    auto missing_cap = sriov::mint_sriov_plan(init_ctx(), nic_identity(), caps, iface(),
                                              sriov::admit_vf_count(1).expect("1 function fits"));
    assert(missing_cap.err() == ::fixy::some(sriov::SrIovError::MissingSrIovCapability));

    auto no_name = sriov::mint_sriov_plan(init_ctx(), nic_identity(), sriov_caps(), cntp::NicInterfaceName{},
                                          sriov::admit_vf_count(1).expect("1 function fits"));
    assert(no_name.err() == ::fixy::some(sriov::SrIovError::InvalidInterfaceName));

    auto query = sriov::query_current(nic_identity(), iface());
    assert(query.err() == ::fixy::some(sriov::SrIovError::QueryDeferred));

    crucible::test::pass("  test_identity_and_capability_gates: PASSED\n");
}

void test_privileged_boundaries() {
    auto plan = two_vf_plan();

    ::foundation::core::FixedArray<sriov::VfHandle, 2> handles{};
    auto enable = sriov::enable(plan, handles.view());
    assert(enable.err() == ::fixy::some(sriov::SrIovError::PrivilegedApplyDeferred));

    auto privileged = two_vf_plan(true);
    auto privileged_enable = sriov::enable(privileged, handles.view());
    assert(privileged_enable.err() == ::fixy::some(sriov::SrIovError::PrivilegedBackendUnavailable));

    auto handle = sriov::vf_handle_at(plan, sriov::first_vf_index).expect("index 0 is below the count of two");
    auto cfg = sriov::declare_vf_config(sriov::VfConfig{});
    auto configure = sriov::configure_vf(handle, cfg);
    assert(configure.err() == ::fixy::some(sriov::SrIovError::PrivilegedApplyDeferred));

    auto empty_configure = sriov::configure_vf(sriov::VfHandle{}, cfg);
    assert(empty_configure.err() == ::fixy::some(sriov::SrIovError::ZeroCog));

    auto disable = sriov::disable(plan);
    assert(disable.err() == ::fixy::some(sriov::SrIovError::PrivilegedApplyDeferred));

    crucible::test::pass("  test_privileged_boundaries: PASSED\n");
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

    ::foundation::core::FixedArray<sriov::VfHandle, 2> handles{};
    auto deferred = sriov::enable(plan, handles.view());
    assert(deferred.err() == ::fixy::some(sriov::SrIovError::PrivilegedApplyDeferred));

    auto requested = two_vf_plan(true);
    auto requested_enable = sriov::enable(requested, handles.view());
    assert(requested_enable.err() == ::fixy::some(sriov::SrIovError::PrivilegedBackendUnavailable));

    auto requested_disable = sriov::disable(requested);
    assert(requested_disable.err() == ::fixy::some(sriov::SrIovError::PrivilegedBackendUnavailable));

    auto handle = sriov::vf_handle_at(plan, sriov::first_vf_index).expect("index 0 is below the count of two");
    auto cfg = sriov::declare_vf_config(sriov::VfConfig{});
    auto configure = sriov::configure_vf(handle, cfg);
    assert(configure.err() == ::fixy::some(sriov::SrIovError::PrivilegedApplyDeferred));

    auto query = sriov::query_current(nic_identity(), iface());
    assert(query.err() == ::fixy::some(sriov::SrIovError::QueryDeferred));

    crucible::test::pass("  test_apply_paths_are_stubbed: PASSED\n");
}

void test_enumerator_names() {
    using ::foundation::reflect::enum_name;
    assert(enum_name(sriov::SrIovError::QueryDeferred) == std::string_view{"QueryDeferred"});
    assert(enum_name(static_cast<sriov::SrIovError>(0xFF)) == std::string_view{"<unknown SrIovError>"});

    crucible::test::pass("  test_enumerator_names: PASSED\n");
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

    ::fixy::report(::fixy::Sink::Out, "test_sriov:\n");
    test_admission();
    test_mint_and_handles();
    test_identity_and_capability_gates();
    test_privileged_boundaries();
    test_apply_paths_are_stubbed();
    test_enumerator_names();
    crucible::test::pass("test_sriov: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
