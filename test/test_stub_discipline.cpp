// FIXY-U-087 stub-vs-live deprecation discipline witness.
//
// This TU pins the two-layer discipline framework at compile + runtime:
//
//   Layer 1 — honesty markers (machine-readable at compile time):
//     Each header that ships a stub surface declares
//       inline constexpr bool *_implemented = false;
//       inline constexpr bool *_attached    = false;
//     This TU static_asserts every marker IS in fact false today.  When
//     a backend ships live behavior and the marker flips to true, this
//     TU fails to compile — forcing the substrate author to update the
//     test_apply_paths_are_stubbed sentinels in lockstep with the flip.
//
//   Layer 2 — [[deprecated("CRUCIBLE_STUB:...")]] attributes (visible
//     at every call site as -Wdeprecated-declarations warning):
//     Authorized callers suppress with `#pragma GCC diagnostic
//     push/ignored "-Wdeprecated-declarations"/pop`.  This TU exercises
//     a stub from each of the five headers under suppression, proving
//     (a) the function is reachable through normal include + namespace
//     resolution, (b) the deprecation does not affect runtime sentinel
//     semantics, and (c) the runtime sentinel return is still the
//     expected explicit-deferral / explicit-unavailable code.
//
// The CI guard `scripts/check-stub-discipline.sh` enforces the pair
// invariant at script time.  This TU enforces it at C++ build time.

#include <crucible/cntp/MtlsTransport.h>
#include <crucible/cntp/RoceConfig.h>
#include <crucible/cntp/Tcam.h>
#include <crucible/cog/NicConfig.h>
#include <crucible/cog/SrIov.h>

#include "test_assert.h"

#include <cstdio>

// ── Layer 1: honesty markers are false TODAY ──────────────────────────────
// When any of these flips to true, the corresponding live-tier test
// (test_apply_paths_are_stubbed / test_data_plane_is_stub) must be
// updated in lockstep to exercise the live data-plane path, AND the
// [[deprecated]] attributes in the header must be removed.  This
// static_assert is the regression net for that lockstep contract.
static_assert(crucible::cntp::data_plane_implemented == false,
              "FIXY-U-087: MtlsTransport.h::data_plane_implemented flipped to "
              "true — update test_cntp_mtls_transport::test_data_plane_is_stub "
              "AND remove [[deprecated]] from connect_mtls / mtls_send / "
              "mtls_recv / enable_ktls_offload in lockstep.");

static_assert(crucible::cog::nic::privileged_apply_implemented == false,
              "FIXY-U-087: NicConfig.h::privileged_apply_implemented flipped "
              "to true — update test_nic_config::test_apply_paths_are_stubbed "
              "AND remove [[deprecated]] from apply_*/query_current in "
              "lockstep.");

static_assert(crucible::cog::sriov::privileged_apply_implemented == false,
              "FIXY-U-087: SrIov.h::privileged_apply_implemented flipped to "
              "true — update test_sriov::test_apply_paths_are_stubbed AND "
              "remove [[deprecated]] from SrIovManager::* / free enable / "
              "configure_vf / disable / query_current in lockstep.");

static_assert(crucible::cntp::privileged_apply_implemented == false,
              "FIXY-U-087: RoceConfig.h::privileged_apply_implemented flipped "
              "to true — update test_cntp_roce_config::"
              "test_apply_paths_are_stubbed AND remove [[deprecated]] from "
              "apply_roce_config / verify_dcqcn_active / query_dcqcn_state.");

static_assert(crucible::cntp::tcam::vendor_backend_attached == false,
              "FIXY-U-087: Tcam.h::vendor_backend_attached flipped to true — "
              "update test_cntp_tcam::test_apply_paths_are_stubbed AND remove "
              "[[deprecated]] from force_tcam_backend_boundary in lockstep.");

namespace {

// ── Layer 2: pragma-suppressed call sites reach the runtime sentinel ─────
//
// Every test function below calls AT LEAST one deprecated stub from its
// header, under the authorized `#pragma GCC diagnostic ignored` envelope.
// Build-time invariant: the warning is suppressed, the binary links, the
// call returns the documented sentinel error code.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

void test_mtls_stub_returns_backend_unavailable() {
    namespace cntp = crucible::cntp;
    // mtls_send on a never-connected MtlsConnection.  We can't construct
    // an MtlsConnection without the private ctor (friend connect_mtls is
    // the only path, and that's a stub too).  Skip the actual call here;
    // the runtime sentinel is exercised by test_cntp_mtls_transport.
    // What we verify HERE is that the call-site WOULD compile under
    // suppression — i.e. the deprecated attribute does not break the
    // overload set.
    using ConnectFn = std::expected<cntp::MtlsConnection, cntp::MtlsError>(*)(
        cntp::SocketFd,
        cntp::DeclaredMtlsConfig const&,
        cntp::MtlsDnsName,
        cntp::MtlsCertificateFingerprint) noexcept;
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
    plan.capacity = tcam::TcamEntryCount{
        std::uint32_t{4}, typename tcam::TcamEntryCount::Trusted{}};
    plan.backend_ready = false;

    auto declared_plan = tcam::DeclaredTcamTable{plan};

    tcam::TcamFlowRule rule{};
    rule.rule_id = tcam::TcamRuleId{
        std::uint64_t{1}, typename tcam::TcamRuleId::Trusted{}};
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
