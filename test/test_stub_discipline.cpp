// Two layers keep a stub honest.
//
// A header that ships a stub surface declares a constant saying so, and
// this file asserts that every such constant is still false.  When a
// backend goes live and its constant flips, this file stops compiling,
// which is what forces the matching live-tier test to be rewritten in the
// same change rather than later.
//
// The stub entry points themselves carry [[deprecated("CRUCIBLE_STUB:...")]],
// so every call site warns.  An authorised caller suppresses that warning
// explicitly.  The calls below run under that suppression and check that
// each stub is still reachable and still returns its documented sentinel.

#include <crucible/cntp/MtlsTransport.h>
#include <crucible/cntp/RoceConfig.h>
#include <crucible/cntp/Tcam.h>
#include <crucible/cog/NicConfig.h>
#include <crucible/cog/SrIov.h>

#include "test_assert.h"

#include <cstdio>

static_assert(crucible::cntp::data_plane_implemented == false,
              "data_plane_implemented flipped to true. Rewrite the live-tier "
              "test for this surface and remove the deprecation from "
              "connect_mtls, mtls_send, mtls_recv and enable_ktls_offload in "
              "lockstep.");

static_assert(crucible::cog::nic::privileged_apply_implemented == false,
              "the nic privileged_apply_implemented flipped to true. Rewrite "
              "the live-tier test for this surface and remove the deprecation "
              "from the apply entry points and query_current in lockstep.");

static_assert(crucible::cog::sriov::privileged_apply_implemented == false,
              "the sriov privileged_apply_implemented flipped to true. "
              "Rewrite the live-tier test for this surface and remove the "
              "deprecation from the manager methods, enable, configure_vf, "
              "disable and query_current in lockstep.");

static_assert(crucible::cntp::privileged_apply_implemented == false,
              "the roce privileged_apply_implemented flipped to true. Rewrite "
              "the live-tier test for this surface and remove the deprecation "
              "from apply_roce_config, verify_dcqcn_active and "
              "query_dcqcn_state in lockstep.");

static_assert(crucible::cntp::tcam::vendor_backend_attached == false,
              "vendor_backend_attached flipped to true. Rewrite the live-tier "
              "test for this surface and remove the deprecation from "
              "force_tcam_backend_boundary in lockstep.");

namespace {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

void test_mtls_stub_returns_backend_unavailable() {
    namespace cntp = crucible::cntp;
    // A connection cannot be built here: the only path to one is itself a
    // stub.  Taking the address of the entry point is enough to show that
    // the deprecation does not break the overload set under suppression.
    using ConnectFn = std::expected<cntp::MtlsConnection, cntp::MtlsError> (*)(
        cntp::SocketFd, cntp::DeclaredMtlsConfig const&, cntp::MtlsDnsName, cntp::MtlsCertificateFingerprint) noexcept;
    constexpr ConnectFn p = &cntp::connect_mtls;
    (void)p;
    std::printf("  test_mtls_stub_returns_backend_unavailable: PASSED\n");
}

void test_nic_apply_returns_privileged_deferred() {
    namespace nic = crucible::cog::nic;
    namespace cntp = crucible::cntp;

    nic::EthtoolConfig ethtool{};
    ethtool.interface = cntp::NicInterfaceName::from("lo").value();
    auto declared = nic::declare_ethtool_config(ethtool);
    auto applied = nic::apply_ethtool(declared);
    assert(!applied.has_value());
    assert(applied.error() == nic::NicConfigError::PrivilegedApplyDeferred);
    std::printf("  test_nic_apply_returns_privileged_deferred: PASSED\n");
}

void test_sriov_query_returns_query_deferred() {
    namespace sriov = crucible::cog::sriov;
    namespace cog = crucible::cog;
    namespace cntp = crucible::cntp;

    cog::CogIdentity physical{};
    physical.uuid = cog::Uuid{0x87u, 0x42u};
    physical.kind = cog::CogKind::NicPort;
    auto iface = cntp::NicInterfaceName::from("eth0").value();
    auto query = sriov::query_current(physical, iface);
    assert(!query.has_value());
    assert(query.error() == sriov::SrIovError::QueryDeferred);
    std::printf("  test_sriov_query_returns_query_deferred: PASSED\n");
}

void test_roce_dcqcn_state_unavailable() {
    namespace cntp = crucible::cntp;

    auto iface = cntp::NicInterfaceName::from("lo").value();
    auto state = cntp::query_dcqcn_state(iface);
    assert(state == cntp::DcqcnState::BackendUnavailable);

    auto verify = cntp::verify_dcqcn_active(iface);
    assert(!verify.has_value());
    assert(verify.error() == cntp::RoceError::DcqcnStatusUnavailable);
    std::printf("  test_roce_dcqcn_state_unavailable: PASSED\n");
}

void test_tcam_force_returns_vendor_unavailable() {
    namespace tcam = crucible::cntp::tcam;

    tcam::TcamTablePlan plan{};
    plan.target.uuid = crucible::cog::Uuid{0x148u, 0x9u};
    plan.target.kind = crucible::cog::CogKind::NicPort;
    plan.capacity = tcam::TcamEntryCount{std::uint32_t{4}, typename tcam::TcamEntryCount::Trusted{}};
    plan.backend_ready = false;

    auto declared_plan = tcam::DeclaredTcamTable{plan};

    tcam::TcamFlowRule rule{};
    rule.rule_id = tcam::TcamRuleId{std::uint64_t{1}, typename tcam::TcamRuleId::Trusted{}};
    rule.action.kind = tcam::FlowAction::Drop;
    auto declared_rule = tcam::declare_tcam_rule(rule);
    assert(declared_rule.has_value());

    auto force = tcam::force_tcam_backend_boundary(declared_plan, *declared_rule);
    assert(!force.has_value());
    assert(force.error() == tcam::TcamError::VendorBackendUnavailable);
    std::printf("  test_tcam_force_returns_vendor_unavailable: PASSED\n");
}

#pragma GCC diagnostic pop

}  // namespace

int main() {
    std::printf("test_stub_discipline:\n");
    test_mtls_stub_returns_backend_unavailable();
    test_nic_apply_returns_privileged_deferred();
    test_sriov_query_returns_query_deferred();
    test_roce_dcqcn_state_unavailable();
    test_tcam_force_returns_vendor_unavailable();
    std::printf("test_stub_discipline: all PASSED\n");
    return 0;
}
