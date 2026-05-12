// Sentinel and runtime smoke for include/crucible/cog/NumaNic.h.
//
// GAPS-194: verifies supplied NIC/NUMA affinity facts only. Sysfs write
// paths for IRQ/RPS/XPS steering are deferred to NicConfig.

#include <crucible/cog/NumaNic.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>

namespace cog = crucible::cog;
namespace safety = crucible::safety;

static cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x194, 0x1};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

static cog::NicPortTargetCaps nic_caps() {
    cog::NicPortTargetCaps caps{};
    caps.link_layer = safety::Tagged<cog::LinkLayer, safety::source::Vendor>{
        cog::LinkLayer::Roce};
    caps.max_tx_queues =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{16};
    caps.max_rx_queues =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{16};
    caps.features.set(cog::NicFeature::Rss);
    caps.features.set(cog::NicFeature::GpuDirectRdma);
    return caps;
}

static cog::NumaNicFacts local_facts() {
    cog::NumaNicFacts facts{};
    facts.nic_node = cog::NumaNodeId{1};
    facts.target_node = cog::NumaNodeId{1};
    facts.irq_handlers = cog::PositiveAffinityCount{8};
    facts.irq_handlers_on_target_node = cog::PositiveAffinityCount{8};
    facts.rx_queues = cog::PositiveAffinityCount{8};
    facts.rx_queues_on_target_node = cog::PositiveAffinityCount{8};
    facts.tx_queues = cog::PositiveAffinityCount{8};
    facts.tx_queues_on_target_node = cog::PositiveAffinityCount{8};
    facts.gpu_direct_peers = cog::PositiveAffinityCount{2};
    facts.gpu_direct_peers_on_target_node = cog::PositiveAffinityCount{2};
    facts.irq_affinity_known = true;
    facts.rps_affinity_known = true;
    facts.xps_affinity_known = true;
    facts.gpu_direct_peer_numa_known = true;
    return facts;
}

static void test_name_accessors() {
    assert(cog::numa_nic_issue_name(cog::NumaNicIssue::IrqRemoteFromTarget)
           == std::string_view{"IrqRemoteFromTarget"});
    std::printf("  test_name_accessors:              PASSED\n");
}

static void test_local_configuration_passes() {
    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(
        nic_identity(), nic_caps(), local_facts());
    assert(report.passes());
    assert(report.effective_target_node == cog::NumaNodeId{1});
    std::printf("  test_local_configuration_passes:  PASSED\n");
}

static void test_remote_nic_is_error() {
    auto facts = local_facts();
    facts.nic_node = cog::NumaNodeId{0};

    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(
        nic_identity(), nic_caps(), facts);
    assert(report.severity == cog::NumaNicSeverity::Error);
    assert(report.has(cog::NumaNicIssue::NicRemoteFromTarget));
    std::printf("  test_remote_nic_is_error:         PASSED\n");
}

static void test_remote_irq_rps_xps_are_warnings() {
    auto facts = local_facts();
    facts.irq_handlers_on_target_node = cog::PositiveAffinityCount{2};
    facts.rx_queues_on_target_node = cog::PositiveAffinityCount{4};
    facts.tx_queues_on_target_node = cog::PositiveAffinityCount{4};
    facts.gpu_direct_peers_on_target_node = cog::PositiveAffinityCount{1};

    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(
        nic_identity(), nic_caps(), facts);
    assert(report.severity == cog::NumaNicSeverity::Warn);
    assert(report.has(cog::NumaNicIssue::IrqRemoteFromTarget));
    assert(report.has(cog::NumaNicIssue::RpsRemoteFromTarget));
    assert(report.has(cog::NumaNicIssue::XpsRemoteFromTarget));
    assert(report.has(cog::NumaNicIssue::GpuDirectPeerRemote));
    std::printf("  test_remote_irq_rps_xps_are_warnings: PASSED\n");
}

static void test_unknown_topology_policy() {
    cog::NumaNicFacts facts{};
    cog::NumaNicPolicy policy{};
    policy.strict_unknown_topology = false;

    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(
        nic_identity(), nic_caps(), facts, policy);
    assert(report.severity == cog::NumaNicSeverity::Warn);
    assert(report.has(cog::NumaNicIssue::NicNumaUnknown));
    assert(report.has(cog::NumaNicIssue::TargetNumaUnknown));
    std::printf("  test_unknown_topology_policy:     PASSED\n");
}

int main() {
    static_assert(cog::NumaNicAuditableCog<cog::CogKind::NicPort>);
    static_assert(!cog::NumaNicAuditableCog<cog::CogKind::Gpu>);
    static_assert(safety::diag::is_diagnostic_class_v<cog::NumaNic_Misaligned>);

    std::printf("test_numa_nic: 5 groups\n");
    test_name_accessors();
    test_local_configuration_passes();
    test_remote_nic_is_error();
    test_remote_irq_rps_xps_are_warnings();
    test_unknown_topology_policy();
    std::printf("test_numa_nic: all passed\n");
    return 0;
}
