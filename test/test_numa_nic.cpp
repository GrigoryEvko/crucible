// Only the supplied affinity facts are checked here. The sysfs write paths
// for interrupt and queue steering are not exercised.

#include <crucible/cog/NumaNic.h>
#include <foundation/reflect/EnumName.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdio>
#include <string_view>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace cog = crucible::cog;

static cog::PositiveAffinityCount count_of(std::uint16_t count) {
    return ::fixy::mint_refined<::fixy::positive>(count);
}

static cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x194, 0x1};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

static cog::NicPortTargetCaps nic_caps() {
    cog::NicPortTargetCaps caps{};
    caps.link_layer = ::fixy::mint_tagged<::fixy::tags::source::Vendor, cog::LinkLayer>(cog::LinkLayer::Roce);
    caps.max_tx_queues = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint16_t>(16);
    caps.max_rx_queues = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint16_t>(16);
    caps.features.set(cog::NicFeature::Rss);
    caps.features.set(cog::NicFeature::GpuDirectRdma);
    return caps;
}

static cog::NumaNicFacts local_facts() {
    cog::NumaNicFacts facts{};
    facts.nic_node = cog::NumaNodeId{1};
    facts.target_node = cog::NumaNodeId{1};
    facts.irq_handlers = count_of(8);
    facts.irq_handlers_on_target_node = count_of(8);
    facts.rx_queues = count_of(8);
    facts.rx_queues_on_target_node = count_of(8);
    facts.tx_queues = count_of(8);
    facts.tx_queues_on_target_node = count_of(8);
    facts.gpu_direct_peers = count_of(2);
    facts.gpu_direct_peers_on_target_node = count_of(2);
    facts.irq_affinity_known = true;
    facts.rps_affinity_known = true;
    facts.xps_affinity_known = true;
    facts.gpu_direct_peer_numa_known = true;
    return facts;
}

static void test_enumerator_names() {
    using ::fixy::enum_name_text;
    CRUCIBLE_TEST_ASSERT(kTest, enum_name_text(cog::NumaNicIssue::IrqRemoteFromTarget)
                                    == ::fixy::TextView{"IrqRemoteFromTarget"});
    CRUCIBLE_TEST_ASSERT(kTest, enum_name_text(static_cast<cog::NumaNicIssue>(1u << 31))
                                    == ::fixy::TextView{"<unknown NumaNicIssue>"});
    ::fixy::check::pass(kTest, "  test_enumerator_names:            PASSED\n");
}

static void test_local_configuration_passes() {
    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(nic_identity(), nic_caps(), local_facts());
    CRUCIBLE_TEST_ASSERT(kTest, report.passes());
    CRUCIBLE_TEST_ASSERT(kTest, report.effective_target_node == cog::NumaNodeId{1});
    ::fixy::check::pass(kTest, "  test_local_configuration_passes:  PASSED\n");
}

static void test_remote_nic_is_error() {
    auto facts = local_facts();
    facts.nic_node = cog::NumaNodeId{0};

    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(nic_identity(), nic_caps(), facts);
    CRUCIBLE_TEST_ASSERT(kTest, report.severity == cog::AuditSeverity::Error);
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NumaNicIssue::NicRemoteFromTarget));
    ::fixy::check::pass(kTest, "  test_remote_nic_is_error:         PASSED\n");
}

static void test_remote_irq_rps_xps_are_warnings() {
    auto facts = local_facts();
    facts.irq_handlers_on_target_node = count_of(2);
    facts.rx_queues_on_target_node = count_of(4);
    facts.tx_queues_on_target_node = count_of(4);
    facts.gpu_direct_peers_on_target_node = cog::single_affinity;

    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(nic_identity(), nic_caps(), facts);
    CRUCIBLE_TEST_ASSERT(kTest, report.severity == cog::AuditSeverity::Warn);
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NumaNicIssue::IrqRemoteFromTarget));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NumaNicIssue::RpsRemoteFromTarget));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NumaNicIssue::XpsRemoteFromTarget));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NumaNicIssue::GpuDirectPeerRemote));
    ::fixy::check::pass(kTest, "  test_remote_irq_rps_xps_are_warnings: PASSED\n");
}

static void test_unknown_topology_policy() {
    cog::NumaNicFacts facts{};
    cog::NumaNicPolicy policy{};
    policy.strict_unknown_topology = false;

    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(nic_identity(), nic_caps(), facts, policy);
    CRUCIBLE_TEST_ASSERT(kTest, report.severity == cog::AuditSeverity::Warn);
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NumaNicIssue::NicNumaUnknown));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NumaNicIssue::TargetNumaUnknown));
    ::fixy::check::pass(kTest, "  test_unknown_topology_policy:     PASSED\n");
}

int main() {
    static_assert(cog::NumaNicAuditableCog<cog::CogKind::NicPort>);
    static_assert(!cog::NumaNicAuditableCog<cog::CogKind::Gpu>);
    static_assert(::foundation::diag::is_diagnostic_class_v<cog::NumaNic_Misaligned>);

    ::fixy::report(::fixy::Sink::Out, "test_numa_nic: 5 groups\n");
    test_enumerator_names();
    test_local_configuration_passes();
    test_remote_nic_is_error();
    test_remote_irq_rps_xps_are_warnings();
    test_unknown_topology_policy();
    ::fixy::check::pass(kTest, "test_numa_nic: all passed\n");
    return 0;
}
