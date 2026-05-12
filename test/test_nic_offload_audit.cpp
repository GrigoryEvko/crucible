// Sentinel and runtime smoke for include/crucible/cog/NicOffloadAudit.h.
//
// GAPS-140: evaluates supplied NIC configuration facts only. No syscalls,
// no remediation, no CAP_NET_ADMIN side effects.

#include <crucible/cog/NicOffloadAudit.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>

namespace cog = crucible::cog;
namespace safety = crucible::safety;

static cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x140, 0x1};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

static cog::NicPortTargetCaps nic_caps() {
    cog::NicPortTargetCaps caps{};
    caps.link_layer = safety::Tagged<cog::LinkLayer, safety::source::Vendor>{
        cog::LinkLayer::Roce};
    caps.line_rate_bytes_per_sec =
        safety::Tagged<std::uint64_t, safety::source::Vendor>{100ull << 30};
    caps.max_tx_queues =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{64};
    caps.max_rx_queues =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{64};
    caps.features.set(cog::NicFeature::Tso);
    caps.features.set(cog::NicFeature::Gso);
    caps.features.set(cog::NicFeature::Gro);
    caps.features.set(cog::NicFeature::Rss);
    caps.features.set(cog::NicFeature::Roce);
    caps.features.set(cog::NicFeature::GpuDirectRdma);
    return caps;
}

static cog::NicOffloadAuditFacts good_facts() {
    cog::NicOffloadAuditFacts facts{};
    facts.enabled_offloads.set(cog::NicFeature::Tso);
    facts.enabled_offloads.set(cog::NicFeature::Gso);
    facts.enabled_offloads.set(cog::NicFeature::Gro);
    facts.enabled_offloads.set(cog::NicFeature::Rss);
    facts.enabled_offloads.set(cog::NicFeature::Roce);
    facts.enabled_offloads.set(cog::NicFeature::GpuDirectRdma);
    facts.configured_tx_queues = cog::PositiveQueueCount{16};
    facts.configured_rx_queues = cog::PositiveQueueCount{16};
    facts.rss_distinct_rx_queues = cog::PositiveQueueCount{16};
    facts.irq_distinct_local_cores = cog::PositiveQueueCount{16};
    facts.rmem_max_bytes = cog::PositiveByteCount{64ull << 20};
    facts.wmem_max_bytes = cog::PositiveByteCount{64ull << 20};
    facts.netdev_budget_packets = cog::PositiveQueueCount{512};
    facts.busy_poll_us = 50;
    facts.rss_hash = cog::NicRssHash::Toeplitz;
    facts.tx_qdisc = cog::NicTxQdisc::Fq;
    facts.rss_four_tuple_hash = true;
    facts.irq_handlers_numa_local = true;
    return facts;
}

static cog::NicOffloadAuditPolicy strict_policy() {
    cog::NicOffloadAuditPolicy policy{};
    policy.min_tx_queues = cog::PositiveQueueCount{8};
    policy.min_rx_queues = cog::PositiveQueueCount{8};
    policy.min_rss_queues = cog::PositiveQueueCount{8};
    policy.min_irq_local_cores = cog::PositiveQueueCount{8};
    policy.expected_bdp_bytes = cog::PositiveByteCount{32ull << 20};
    policy.min_netdev_budget_packets = cog::PositiveQueueCount{256};
    policy.min_busy_poll_us = 25;
    policy.low_latency_profile = true;
    return policy;
}

static void test_name_accessors() {
    assert(cog::nic_rss_hash_name(cog::NicRssHash::Toeplitz)
           == std::string_view{"Toeplitz"});
    assert(cog::nic_tx_qdisc_name(cog::NicTxQdisc::Fq)
           == std::string_view{"Fq"});
    assert(cog::nic_audit_issue_name(cog::NicAuditIssue::RssDisabled)
           == std::string_view{"RssDisabled"});
    std::printf("  test_name_accessors:                  PASSED\n");
}

static void test_good_configuration_passes() {
    auto const report = cog::audit_nic_offloads<cog::CogKind::NicPort>(
        nic_identity(), nic_caps(), good_facts(), strict_policy());
    assert(report.passes());
    assert(report.missing_required_offloads.none());
    assert(report.unsupported_required_offloads.none());
    std::printf("  test_good_configuration_passes:       PASSED\n");
}

static void test_missing_required_offload_is_error() {
    auto facts = good_facts();
    facts.enabled_offloads.unset(cog::NicFeature::Tso);

    auto const report = cog::audit_nic_offloads<cog::CogKind::NicPort>(
        nic_identity(), nic_caps(), facts, strict_policy());
    assert(!report.passes());
    assert(report.severity == cog::NicAuditSeverity::Error);
    assert(report.has(cog::NicAuditIssue::MissingRequiredOffload));
    assert(report.missing_required_offloads.test(cog::NicFeature::Tso));
    std::printf("  test_missing_required_offload_is_error: PASSED\n");
}

static void test_unsupported_required_offload_is_error() {
    auto caps = nic_caps();
    caps.features.unset(cog::NicFeature::Gso);

    auto const report = cog::audit_nic_offloads<cog::CogKind::NicPort>(
        nic_identity(), caps, good_facts(), strict_policy());
    assert(report.severity == cog::NicAuditSeverity::Error);
    assert(report.has(cog::NicAuditIssue::UnsupportedRequiredOffload));
    assert(report.unsupported_required_offloads.test(cog::NicFeature::Gso));
    std::printf("  test_unsupported_required_offload_is_error: PASSED\n");
}

static void test_rss_and_policy_warnings() {
    auto facts = good_facts();
    facts.rss_distinct_rx_queues = cog::PositiveQueueCount{1};
    facts.rss_hash = cog::NicRssHash::Xor;
    facts.rss_four_tuple_hash = false;
    facts.irq_handlers_numa_local = false;
    facts.rmem_max_bytes = cog::PositiveByteCount{1};
    facts.wmem_max_bytes = cog::PositiveByteCount{1};
    facts.netdev_budget_packets = cog::PositiveQueueCount{1};
    facts.tx_qdisc = cog::NicTxQdisc::Pfifo;
    facts.busy_poll_us = 0;

    auto const report = cog::audit_nic_offloads<cog::CogKind::NicPort>(
        nic_identity(), nic_caps(), facts, strict_policy());
    assert(report.severity == cog::NicAuditSeverity::Warn);
    assert(report.has(cog::NicAuditIssue::RssSpreadTooNarrow));
    assert(report.has(cog::NicAuditIssue::RssHashNotToeplitz));
    assert(report.has(cog::NicAuditIssue::RssFourTupleMissing));
    assert(report.has(cog::NicAuditIssue::IrqRemoteNuma));
    assert(report.has(cog::NicAuditIssue::RmemMaxBelowBdp));
    assert(report.has(cog::NicAuditIssue::WmemMaxBelowBdp));
    assert(report.has(cog::NicAuditIssue::NetdevBudgetTooSmall));
    assert(report.has(cog::NicAuditIssue::TxQdiscNotFq));
    assert(report.has(cog::NicAuditIssue::BusyPollMissing));
    std::printf("  test_rss_and_policy_warnings:         PASSED\n");
}

int main() {
    static_assert(cog::NicOffloadAuditableCog<cog::CogKind::NicPort>);
    static_assert(!cog::NicOffloadAuditableCog<cog::CogKind::Gpu>);
    static_assert(safety::diag::is_diagnostic_class_v<
        cog::NicOffload_Misconfigured>);

    std::printf("test_nic_offload_audit: 5 groups\n");
    test_name_accessors();
    test_good_configuration_passes();
    test_missing_required_offload_is_error();
    test_unsupported_required_offload_is_error();
    test_rss_and_policy_warnings();
    std::printf("test_nic_offload_audit: all passed\n");
    return 0;
}

