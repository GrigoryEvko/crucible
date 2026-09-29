// Only the supplied affinity facts are checked here. The sysfs write paths
// for interrupt and queue steering are not exercised.

#include <crucible/cog/NumaNic.h>
#include <foundation/reflect/EnumName.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>

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
    using ::foundation::reflect::enum_name;
    assert(enum_name(cog::NumaNicIssue::IrqRemoteFromTarget) == std::string_view{"IrqRemoteFromTarget"});
    assert(enum_name(static_cast<cog::NumaNicIssue>(1u << 31)) == std::string_view{"<unknown NumaNicIssue>"});
    std::printf("  test_enumerator_names:            PASSED\n");
}

static void test_local_configuration_passes() {
    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(nic_identity(), nic_caps(), local_facts());
    assert(report.passes());
    assert(report.effective_target_node == cog::NumaNodeId{1});
    std::printf("  test_local_configuration_passes:  PASSED\n");
}

static void test_remote_nic_is_error() {
    auto facts = local_facts();
    facts.nic_node = cog::NumaNodeId{0};

    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(nic_identity(), nic_caps(), facts);
    assert(report.severity == cog::AuditSeverity::Error);
    assert(report.has(cog::NumaNicIssue::NicRemoteFromTarget));
    std::printf("  test_remote_nic_is_error:         PASSED\n");
}

static void test_remote_irq_rps_xps_are_warnings() {
    auto facts = local_facts();
    facts.irq_handlers_on_target_node = count_of(2);
    facts.rx_queues_on_target_node = count_of(4);
    facts.tx_queues_on_target_node = count_of(4);
    facts.gpu_direct_peers_on_target_node = cog::single_affinity;

    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(nic_identity(), nic_caps(), facts);
    assert(report.severity == cog::AuditSeverity::Warn);
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

    auto const report = cog::verify_numa_pinning<cog::CogKind::NicPort>(nic_identity(), nic_caps(), facts, policy);
    assert(report.severity == cog::AuditSeverity::Warn);
    assert(report.has(cog::NumaNicIssue::NicNumaUnknown));
    assert(report.has(cog::NumaNicIssue::TargetNumaUnknown));
    std::printf("  test_unknown_topology_policy:     PASSED\n");
}

int main() {
    static_assert(cog::NumaNicAuditableCog<cog::CogKind::NicPort>);
    static_assert(!cog::NumaNicAuditableCog<cog::CogKind::Gpu>);
    static_assert(::foundation::diag::is_diagnostic_class_v<cog::NumaNic_Misaligned>);

    std::printf("test_numa_nic: 5 groups\n");
    test_enumerator_names();
    test_local_configuration_passes();
    test_remote_nic_is_error();
    test_remote_irq_rps_xps_are_warnings();
    test_unknown_topology_policy();
    std::printf("test_numa_nic: all passed\n");
    return 0;
}
