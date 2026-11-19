// The audit reads the configuration facts it is handed and nothing else.
// It issues no syscalls, changes no setting, and needs no privilege.

#include <crucible/cog/NicOffloadAudit.h>
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

static cog::PositiveQueueCount queues(std::uint16_t count) { return ::fixy::mint_refined<::fixy::positive>(count); }

static cog::PositiveByteCount bytes(std::uint64_t count) { return ::fixy::mint_refined<::fixy::positive>(count); }

static cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x140, 0x1};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

static cog::NicPortTargetCaps nic_caps() {
    cog::NicPortTargetCaps caps{};
    caps.link_layer = ::fixy::mint_tagged<::fixy::tags::source::Vendor, cog::LinkLayer>(cog::LinkLayer::Roce);
    caps.line_rate_bytes_per_sec = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(100ull << 30);
    caps.max_tx_queues = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint16_t>(64);
    caps.max_rx_queues = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint16_t>(64);
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
    facts.configured_tx_queues = queues(16);
    facts.configured_rx_queues = queues(16);
    facts.rss_distinct_rx_queues = queues(16);
    facts.irq_distinct_local_cores = queues(16);
    facts.rmem_max_bytes = bytes(64ull << 20);
    facts.wmem_max_bytes = bytes(64ull << 20);
    facts.netdev_budget_packets = queues(512);
    facts.busy_poll_us = 50;
    facts.rss_hash = cog::NicRssHash::Toeplitz;
    facts.tx_qdisc = cog::NicTxQdisc::Fq;
    facts.rss_four_tuple_hash = true;
    facts.irq_handlers_numa_local = true;
    return facts;
}

static cog::NicOffloadAuditPolicy strict_policy() {
    cog::NicOffloadAuditPolicy policy{};
    policy.min_tx_queues = queues(8);
    policy.min_rx_queues = queues(8);
    policy.min_rss_queues = queues(8);
    policy.min_irq_local_cores = queues(8);
    policy.expected_bdp_bytes = bytes(32ull << 20);
    policy.min_netdev_budget_packets = queues(256);
    policy.min_busy_poll_us = 25;
    policy.low_latency_profile = true;
    return policy;
}

static void test_enumerator_names() {
    using ::foundation::reflect::enum_name;
    CRUCIBLE_TEST_ASSERT(kTest, enum_name(cog::NicRssHash::Toeplitz) == std::string_view{"Toeplitz"});
    CRUCIBLE_TEST_ASSERT(kTest, enum_name(cog::NicTxQdisc::Fq) == std::string_view{"Fq"});
    CRUCIBLE_TEST_ASSERT(kTest, enum_name(cog::NicAuditIssue::RssDisabled) == std::string_view{"RssDisabled"});
    CRUCIBLE_TEST_ASSERT(kTest, enum_name(static_cast<cog::NicAuditIssue>(1u << 31))
                                    == std::string_view{"<unknown NicAuditIssue>"});
    ::fixy::check::pass(kTest, "  test_enumerator_names:                PASSED\n");
}

static void test_good_configuration_passes() {
    auto const report =
        cog::audit_nic_offloads<cog::CogKind::NicPort>(nic_identity(), nic_caps(), good_facts(), strict_policy());
    CRUCIBLE_TEST_ASSERT(kTest, report.passes());
    CRUCIBLE_TEST_ASSERT(kTest, report.missing_required_offloads.none());
    CRUCIBLE_TEST_ASSERT(kTest, report.unsupported_required_offloads.none());
    ::fixy::check::pass(kTest, "  test_good_configuration_passes:       PASSED\n");
}

static void test_missing_required_offload_is_error() {
    auto facts = good_facts();
    facts.enabled_offloads.unset(cog::NicFeature::Tso);

    auto const report =
        cog::audit_nic_offloads<cog::CogKind::NicPort>(nic_identity(), nic_caps(), facts, strict_policy());
    CRUCIBLE_TEST_ASSERT(kTest, !report.passes());
    CRUCIBLE_TEST_ASSERT(kTest, report.severity == cog::AuditSeverity::Error);
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::MissingRequiredOffload));
    CRUCIBLE_TEST_ASSERT(kTest, report.missing_required_offloads.test(cog::NicFeature::Tso));
    ::fixy::check::pass(kTest, "  test_missing_required_offload_is_error: PASSED\n");
}

static void test_later_warning_keeps_error() {
    auto facts = good_facts();
    facts.enabled_offloads.unset(cog::NicFeature::Tso);
    facts.tx_qdisc = cog::NicTxQdisc::Pfifo;

    auto const report =
        cog::audit_nic_offloads<cog::CogKind::NicPort>(nic_identity(), nic_caps(), facts, strict_policy());
    CRUCIBLE_TEST_ASSERT(kTest, report.severity == cog::AuditSeverity::Error);
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::MissingRequiredOffload));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::TxQdiscNotFq));
    ::fixy::check::pass(kTest, "  test_later_warning_keeps_error:       PASSED\n");
}

static void test_unsupported_required_offload_is_error() {
    auto caps = nic_caps();
    caps.features.unset(cog::NicFeature::Gso);

    auto const report =
        cog::audit_nic_offloads<cog::CogKind::NicPort>(nic_identity(), caps, good_facts(), strict_policy());
    CRUCIBLE_TEST_ASSERT(kTest, report.severity == cog::AuditSeverity::Error);
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::UnsupportedRequiredOffload));
    CRUCIBLE_TEST_ASSERT(kTest, report.unsupported_required_offloads.test(cog::NicFeature::Gso));
    ::fixy::check::pass(kTest, "  test_unsupported_required_offload_is_error: PASSED\n");
}

static void test_rss_and_policy_warnings() {
    auto facts = good_facts();
    facts.rss_distinct_rx_queues = cog::single_queue;
    facts.rss_hash = cog::NicRssHash::Xor;
    facts.rss_four_tuple_hash = false;
    facts.irq_handlers_numa_local = false;
    facts.rmem_max_bytes = cog::single_byte;
    facts.wmem_max_bytes = cog::single_byte;
    facts.netdev_budget_packets = cog::single_queue;
    facts.tx_qdisc = cog::NicTxQdisc::Pfifo;
    facts.busy_poll_us = 0;

    auto const report =
        cog::audit_nic_offloads<cog::CogKind::NicPort>(nic_identity(), nic_caps(), facts, strict_policy());
    CRUCIBLE_TEST_ASSERT(kTest, report.severity == cog::AuditSeverity::Warn);
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::RssSpreadTooNarrow));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::RssHashNotToeplitz));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::RssFourTupleMissing));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::IrqRemoteNuma));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::RmemMaxBelowBdp));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::WmemMaxBelowBdp));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::NetdevBudgetTooSmall));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::TxQdiscNotFq));
    CRUCIBLE_TEST_ASSERT(kTest, report.has(cog::NicAuditIssue::BusyPollMissing));
    ::fixy::check::pass(kTest, "  test_rss_and_policy_warnings:         PASSED\n");
}

int main() {
    static_assert(cog::NicOffloadAuditableCog<cog::CogKind::NicPort>);
    static_assert(!cog::NicOffloadAuditableCog<cog::CogKind::Gpu>);
    static_assert(::foundation::diag::is_diagnostic_class_v<cog::NicOffload_Misconfigured>);

    ::fixy::report(::fixy::Sink::Out, "test_nic_offload_audit: 6 groups\n");
    test_enumerator_names();
    test_good_configuration_passes();
    test_missing_required_offload_is_error();
    test_later_warning_keeps_error();
    test_unsupported_required_offload_is_error();
    test_rss_and_policy_warnings();
    ::fixy::check::pass(kTest, "test_nic_offload_audit: all passed\n");
    return 0;
}
