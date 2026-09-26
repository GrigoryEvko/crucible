#pragma once

// A read-only audit of a NIC against a policy. The facts are gathered
// elsewhere and handed in, so nothing here queries or changes the
// device. The same facts and policy always yield the same report.
//
// foundation::reflect::enum_name gives the name of each enumerator.

#include <crucible/cog/AuditFindings.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Bits.h>
#include <fixy/Refined.h>
#include <foundation/diag/Catalog.h>

#include <cstdint>
#include <string_view>

namespace crucible::cog {

using PositiveQueueCount = ::fixy::Positive<std::uint16_t>;
using PositiveByteCount = ::fixy::Positive<std::uint64_t>;

// The floor of every count. A fact or a policy field starts here until
// the caller supplies a measured or chosen value.
inline constexpr PositiveQueueCount single_queue = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{1});
inline constexpr PositiveByteCount single_byte = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1});

enum class NicRssHash : std::uint8_t {
    Unknown = 0,
    Toeplitz = 1,
    Xor = 2,
    Crc32 = 3,
};

enum class NicTxQdisc : std::uint8_t {
    Unknown = 0,
    Fq = 1,
    FqCodel = 2,
    Pfifo = 3,
    Mq = 4,
};

enum class NicAuditIssue : std::uint32_t {
    WrongCogKind = 1u << 0,
    UnsupportedRequiredOffload = 1u << 1,
    MissingRequiredOffload = 1u << 2,
    MissingPerformanceOffload = 1u << 3,
    TxQueueCountMisconfigured = 1u << 4,
    RxQueueCountMisconfigured = 1u << 5,
    RssDisabled = 1u << 6,
    RssSpreadTooNarrow = 1u << 7,
    RssHashNotToeplitz = 1u << 8,
    RssFourTupleMissing = 1u << 9,
    IrqSpreadTooNarrow = 1u << 10,
    IrqRemoteNuma = 1u << 11,
    RmemMaxBelowBdp = 1u << 12,
    WmemMaxBelowBdp = 1u << 13,
    NetdevBudgetTooSmall = 1u << 14,
    TxQdiscNotFq = 1u << 15,
    BusyPollMissing = 1u << 16,
};

struct NicOffload_Misconfigured : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "NicOffload_Misconfigured";
    static constexpr std::string_view description = "A NIC startup audit found an offload, queue, RSS, IRQ, sysctl, "
                                                    "qdisc, or busy-poll setting that cannot deliver the declared "
                                                    "network throughput envelope.";
    static constexpr std::string_view remediation = "Inspect the report issue bits, then apply the corresponding "
                                                    "NicConfig/NumaNic remediation under operator policy. Keep the "
                                                    "audit read-only unless CAP_NET_ADMIN remediation is explicitly "
                                                    "enabled by a later configuration task.";
};

struct NicOffloadAuditFacts {
    ::fixy::Bits<NicFeature> enabled_offloads{};
    PositiveQueueCount configured_tx_queues = single_queue;
    PositiveQueueCount configured_rx_queues = single_queue;
    PositiveQueueCount rss_distinct_rx_queues = single_queue;
    PositiveQueueCount irq_distinct_local_cores = single_queue;
    PositiveByteCount rmem_max_bytes = single_byte;
    PositiveByteCount wmem_max_bytes = single_byte;
    PositiveQueueCount netdev_budget_packets = single_queue;
    std::uint32_t busy_poll_us = 0;
    NicRssHash rss_hash = NicRssHash::Unknown;
    NicTxQdisc tx_qdisc = NicTxQdisc::Unknown;
    bool rss_four_tuple_hash = false;
    bool irq_handlers_numa_local = false;
};

struct NicOffloadAuditPolicy {
    ::fixy::Bits<NicFeature> required_offloads{
        NicFeature::Tso,
        NicFeature::Gso,
        NicFeature::Gro,
        NicFeature::Rss,
    };
    ::fixy::Bits<NicFeature> performance_offloads{};
    PositiveQueueCount min_tx_queues = single_queue;
    PositiveQueueCount min_rx_queues = single_queue;
    PositiveQueueCount min_rss_queues = single_queue;
    PositiveQueueCount min_irq_local_cores = single_queue;
    PositiveByteCount expected_bdp_bytes = single_byte;
    PositiveQueueCount min_netdev_budget_packets = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{64});
    std::uint32_t min_busy_poll_us = 0;
    bool require_rss = true;
    bool require_toeplitz = true;
    bool require_four_tuple_hash = true;
    bool require_numa_local_irqs = true;
    bool require_fq_qdisc = true;
    bool low_latency_profile = false;
};

struct NicOffloadAuditReport : AuditFindings<NicAuditIssue> {
    ::fixy::Bits<NicFeature> missing_required_offloads{};
    ::fixy::Bits<NicFeature> unsupported_required_offloads{};
    ::fixy::Bits<NicFeature> missing_performance_offloads{};
};

template <CogKind K>
concept NicOffloadAuditableCog = (K == CogKind::NicPort) && HasCaps<K>;

template <CogKind K>
    requires NicOffloadAuditableCog<K>
[[nodiscard]] constexpr NicOffloadAuditReport audit_nic_offloads(CogIdentity const& identity, caps_for_t<K> const& caps,
                                                                 NicOffloadAuditFacts const& facts,
                                                                 NicOffloadAuditPolicy const& policy = {}) noexcept {
    NicOffloadAuditReport report{};

    if (identity.kind != CogKind::NicPort) {
        report.raise(NicAuditIssue::WrongCogKind, AuditSeverity::Error);
    }

    report.unsupported_required_offloads = policy.required_offloads & ~caps.features;
    if (report.unsupported_required_offloads.any()) {
        report.raise(NicAuditIssue::UnsupportedRequiredOffload, AuditSeverity::Error);
    }

    report.missing_required_offloads = policy.required_offloads & ~facts.enabled_offloads;
    if (report.missing_required_offloads.any()) {
        report.raise(NicAuditIssue::MissingRequiredOffload, AuditSeverity::Error);
    }

    report.missing_performance_offloads = policy.performance_offloads & ~facts.enabled_offloads;
    if (report.missing_performance_offloads.any()) {
        report.raise(NicAuditIssue::MissingPerformanceOffload, AuditSeverity::Warn);
    }

    auto const max_tx = caps.max_tx_queues.value();
    auto const max_rx = caps.max_rx_queues.value();
    auto const tx = facts.configured_tx_queues.value();
    auto const rx = facts.configured_rx_queues.value();
    if (tx < policy.min_tx_queues.value() || (max_tx != 0 && tx > max_tx)) {
        report.raise(NicAuditIssue::TxQueueCountMisconfigured,
                     max_tx != 0 && tx > max_tx ? AuditSeverity::Error : AuditSeverity::Warn);
    }
    if (rx < policy.min_rx_queues.value() || (max_rx != 0 && rx > max_rx)) {
        report.raise(NicAuditIssue::RxQueueCountMisconfigured,
                     max_rx != 0 && rx > max_rx ? AuditSeverity::Error : AuditSeverity::Warn);
    }

    if (policy.require_rss && !facts.enabled_offloads.test(NicFeature::Rss)) {
        report.raise(NicAuditIssue::RssDisabled, AuditSeverity::Error);
    }
    if (facts.rss_distinct_rx_queues.value() < policy.min_rss_queues.value()) {
        report.raise(NicAuditIssue::RssSpreadTooNarrow, AuditSeverity::Warn);
    }
    if (policy.require_toeplitz && facts.rss_hash != NicRssHash::Toeplitz) {
        report.raise(NicAuditIssue::RssHashNotToeplitz, AuditSeverity::Warn);
    }
    if (policy.require_four_tuple_hash && !facts.rss_four_tuple_hash) {
        report.raise(NicAuditIssue::RssFourTupleMissing, AuditSeverity::Warn);
    }

    if (facts.irq_distinct_local_cores.value() < policy.min_irq_local_cores.value()) {
        report.raise(NicAuditIssue::IrqSpreadTooNarrow, AuditSeverity::Warn);
    }
    if (policy.require_numa_local_irqs && !facts.irq_handlers_numa_local) {
        report.raise(NicAuditIssue::IrqRemoteNuma, AuditSeverity::Warn);
    }

    if (facts.rmem_max_bytes.value() < policy.expected_bdp_bytes.value()) {
        report.raise(NicAuditIssue::RmemMaxBelowBdp, AuditSeverity::Warn);
    }
    if (facts.wmem_max_bytes.value() < policy.expected_bdp_bytes.value()) {
        report.raise(NicAuditIssue::WmemMaxBelowBdp, AuditSeverity::Warn);
    }
    if (facts.netdev_budget_packets.value() < policy.min_netdev_budget_packets.value()) {
        report.raise(NicAuditIssue::NetdevBudgetTooSmall, AuditSeverity::Warn);
    }
    if (policy.require_fq_qdisc && facts.tx_qdisc != NicTxQdisc::Fq) {
        report.raise(NicAuditIssue::TxQdiscNotFq, AuditSeverity::Warn);
    }
    if (policy.low_latency_profile && facts.busy_poll_us < policy.min_busy_poll_us) {
        report.raise(NicAuditIssue::BusyPollMissing, AuditSeverity::Warn);
    }

    return report;
}

}  // namespace crucible::cog
