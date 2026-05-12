#pragma once

// GAPS-140.  Read-only NIC startup audit over already-harvested facts.
// Discovery/configuration syscalls live in later NicConfig/NumaNic tasks;
// this header is the deterministic policy evaluator that those harvesters
// feed.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/Refined.h>

#include <cstdint>
#include <string_view>

namespace crucible::cog {

using PositiveQueueCount = safety::Positive<std::uint16_t>;
using PositiveByteCount  = safety::Positive<std::uint64_t>;

enum class NicRssHash : std::uint8_t {
    Unknown  = 0,
    Toeplitz = 1,
    Xor      = 2,
    Crc32    = 3,
};

[[nodiscard]] constexpr std::string_view
nic_rss_hash_name(NicRssHash h) noexcept {
    switch (h) {
        case NicRssHash::Unknown:  return "Unknown";
        case NicRssHash::Toeplitz: return "Toeplitz";
        case NicRssHash::Xor:      return "Xor";
        case NicRssHash::Crc32:    return "Crc32";
        default:                   return "<unknown NicRssHash>";
    }
}

enum class NicTxQdisc : std::uint8_t {
    Unknown = 0,
    Fq      = 1,
    FqCodel = 2,
    Pfifo   = 3,
    Mq      = 4,
};

[[nodiscard]] constexpr std::string_view
nic_tx_qdisc_name(NicTxQdisc q) noexcept {
    switch (q) {
        case NicTxQdisc::Unknown: return "Unknown";
        case NicTxQdisc::Fq:      return "Fq";
        case NicTxQdisc::FqCodel: return "FqCodel";
        case NicTxQdisc::Pfifo:   return "Pfifo";
        case NicTxQdisc::Mq:      return "Mq";
        default:                  return "<unknown NicTxQdisc>";
    }
}

enum class NicAuditIssue : std::uint32_t {
    WrongCogKind                 = 1u <<  0,
    UnsupportedRequiredOffload   = 1u <<  1,
    MissingRequiredOffload       = 1u <<  2,
    MissingPerformanceOffload    = 1u <<  3,
    TxQueueCountMisconfigured    = 1u <<  4,
    RxQueueCountMisconfigured    = 1u <<  5,
    RssDisabled                  = 1u <<  6,
    RssSpreadTooNarrow           = 1u <<  7,
    RssHashNotToeplitz           = 1u <<  8,
    RssFourTupleMissing          = 1u <<  9,
    IrqSpreadTooNarrow           = 1u << 10,
    IrqRemoteNuma                = 1u << 11,
    RmemMaxBelowBdp              = 1u << 12,
    WmemMaxBelowBdp              = 1u << 13,
    NetdevBudgetTooSmall         = 1u << 14,
    TxQdiscNotFq                 = 1u << 15,
    BusyPollMissing              = 1u << 16,
};

[[nodiscard]] constexpr std::string_view
nic_audit_issue_name(NicAuditIssue issue) noexcept {
    switch (issue) {
        case NicAuditIssue::WrongCogKind:               return "WrongCogKind";
        case NicAuditIssue::UnsupportedRequiredOffload: return "UnsupportedRequiredOffload";
        case NicAuditIssue::MissingRequiredOffload:     return "MissingRequiredOffload";
        case NicAuditIssue::MissingPerformanceOffload:  return "MissingPerformanceOffload";
        case NicAuditIssue::TxQueueCountMisconfigured:  return "TxQueueCountMisconfigured";
        case NicAuditIssue::RxQueueCountMisconfigured:  return "RxQueueCountMisconfigured";
        case NicAuditIssue::RssDisabled:                return "RssDisabled";
        case NicAuditIssue::RssSpreadTooNarrow:         return "RssSpreadTooNarrow";
        case NicAuditIssue::RssHashNotToeplitz:         return "RssHashNotToeplitz";
        case NicAuditIssue::RssFourTupleMissing:        return "RssFourTupleMissing";
        case NicAuditIssue::IrqSpreadTooNarrow:         return "IrqSpreadTooNarrow";
        case NicAuditIssue::IrqRemoteNuma:              return "IrqRemoteNuma";
        case NicAuditIssue::RmemMaxBelowBdp:            return "RmemMaxBelowBdp";
        case NicAuditIssue::WmemMaxBelowBdp:            return "WmemMaxBelowBdp";
        case NicAuditIssue::NetdevBudgetTooSmall:       return "NetdevBudgetTooSmall";
        case NicAuditIssue::TxQdiscNotFq:               return "TxQdiscNotFq";
        case NicAuditIssue::BusyPollMissing:            return "BusyPollMissing";
        default:                                        return "<unknown NicAuditIssue>";
    }
}

enum class NicAuditSeverity : std::uint8_t {
    Pass  = 0,
    Warn  = 1,
    Error = 2,
};

struct NicOffload_Misconfigured : safety::diag::tag_base {
    static constexpr std::string_view name = "NicOffload_Misconfigured";
    static constexpr std::string_view description =
        "A NIC startup audit found an offload, queue, RSS, IRQ, sysctl, "
        "qdisc, or busy-poll setting that cannot deliver the declared "
        "network throughput envelope.";
    static constexpr std::string_view remediation =
        "Inspect the report issue bits, then apply the corresponding "
        "NicConfig/NumaNic remediation under operator policy. Keep the "
        "audit read-only unless CAP_NET_ADMIN remediation is explicitly "
        "enabled by a later configuration task.";
};

struct NicOffloadAuditFacts {
    safety::Bits<NicFeature> enabled_offloads{};
    PositiveQueueCount configured_tx_queues{std::uint16_t{1}};
    PositiveQueueCount configured_rx_queues{std::uint16_t{1}};
    PositiveQueueCount rss_distinct_rx_queues{std::uint16_t{1}};
    PositiveQueueCount irq_distinct_local_cores{std::uint16_t{1}};
    PositiveByteCount rmem_max_bytes{std::uint64_t{1}};
    PositiveByteCount wmem_max_bytes{std::uint64_t{1}};
    PositiveQueueCount netdev_budget_packets{std::uint16_t{1}};
    std::uint32_t busy_poll_us = 0;
    NicRssHash rss_hash = NicRssHash::Unknown;
    NicTxQdisc tx_qdisc = NicTxQdisc::Unknown;
    bool rss_four_tuple_hash = false;
    bool irq_handlers_numa_local = false;
};

struct NicOffloadAuditPolicy {
    safety::Bits<NicFeature> required_offloads{
        NicFeature::Tso,
        NicFeature::Gso,
        NicFeature::Gro,
        NicFeature::Rss,
    };
    safety::Bits<NicFeature> performance_offloads{};
    PositiveQueueCount min_tx_queues{std::uint16_t{1}};
    PositiveQueueCount min_rx_queues{std::uint16_t{1}};
    PositiveQueueCount min_rss_queues{std::uint16_t{1}};
    PositiveQueueCount min_irq_local_cores{std::uint16_t{1}};
    PositiveByteCount expected_bdp_bytes{std::uint64_t{1}};
    PositiveQueueCount min_netdev_budget_packets{std::uint16_t{64}};
    std::uint32_t min_busy_poll_us = 0;
    bool require_rss = true;
    bool require_toeplitz = true;
    bool require_four_tuple_hash = true;
    bool require_numa_local_irqs = true;
    bool require_fq_qdisc = true;
    bool low_latency_profile = false;
};

struct NicOffloadAuditReport {
    safety::Bits<NicAuditIssue> issues{};
    NicAuditSeverity severity = NicAuditSeverity::Pass;
    safety::Bits<NicFeature> missing_required_offloads{};
    safety::Bits<NicFeature> unsupported_required_offloads{};
    safety::Bits<NicFeature> missing_performance_offloads{};

    [[nodiscard]] constexpr bool passes() const noexcept {
        return severity == NicAuditSeverity::Pass && issues.none();
    }

    [[nodiscard]] constexpr bool has(NicAuditIssue issue) const noexcept {
        return issues.test(issue);
    }
};

template <CogKind K>
concept NicOffloadAuditableCog = (K == CogKind::NicPort) && HasCaps<K>;

namespace detail {

[[nodiscard]] constexpr safety::Bits<NicFeature>
difference(safety::Bits<NicFeature> lhs,
           safety::Bits<NicFeature> rhs) noexcept {
    return safety::Bits<NicFeature>::from_raw(lhs.raw() & ~rhs.raw());
}

constexpr void
raise(NicOffloadAuditReport& report,
      NicAuditIssue issue,
      NicAuditSeverity severity) noexcept {
    report.issues.set(issue);
    if (static_cast<std::uint8_t>(severity)
        > static_cast<std::uint8_t>(report.severity)) {
        report.severity = severity;
    }
}

}  // namespace detail

template <CogKind K>
    requires NicOffloadAuditableCog<K>
[[nodiscard]] constexpr NicOffloadAuditReport
audit_nic_offloads(CogIdentity const& identity,
                   caps_for_t<K> const& caps,
                   NicOffloadAuditFacts const& facts,
                   NicOffloadAuditPolicy const& policy = {}) noexcept {
    NicOffloadAuditReport report{};

    if (identity.kind != CogKind::NicPort) {
        detail::raise(report, NicAuditIssue::WrongCogKind, NicAuditSeverity::Error);
    }

    report.unsupported_required_offloads =
        detail::difference(policy.required_offloads, caps.features);
    if (report.unsupported_required_offloads.any()) {
        detail::raise(report, NicAuditIssue::UnsupportedRequiredOffload,
            NicAuditSeverity::Error);
    }

    report.missing_required_offloads =
        detail::difference(policy.required_offloads, facts.enabled_offloads);
    if (report.missing_required_offloads.any()) {
        detail::raise(report, NicAuditIssue::MissingRequiredOffload,
            NicAuditSeverity::Error);
    }

    report.missing_performance_offloads =
        detail::difference(policy.performance_offloads, facts.enabled_offloads);
    if (report.missing_performance_offloads.any()) {
        detail::raise(report, NicAuditIssue::MissingPerformanceOffload,
            NicAuditSeverity::Warn);
    }

    auto const max_tx = caps.max_tx_queues.value();
    auto const max_rx = caps.max_rx_queues.value();
    auto const tx = facts.configured_tx_queues.value();
    auto const rx = facts.configured_rx_queues.value();
    if (tx < policy.min_tx_queues.value() || (max_tx != 0 && tx > max_tx)) {
        detail::raise(report, NicAuditIssue::TxQueueCountMisconfigured,
            max_tx != 0 && tx > max_tx ? NicAuditSeverity::Error
                                       : NicAuditSeverity::Warn);
    }
    if (rx < policy.min_rx_queues.value() || (max_rx != 0 && rx > max_rx)) {
        detail::raise(report, NicAuditIssue::RxQueueCountMisconfigured,
            max_rx != 0 && rx > max_rx ? NicAuditSeverity::Error
                                       : NicAuditSeverity::Warn);
    }

    if (policy.require_rss
        && !facts.enabled_offloads.test(NicFeature::Rss)) {
        detail::raise(report, NicAuditIssue::RssDisabled, NicAuditSeverity::Error);
    }
    if (facts.rss_distinct_rx_queues.value() < policy.min_rss_queues.value()) {
        detail::raise(report, NicAuditIssue::RssSpreadTooNarrow,
            NicAuditSeverity::Warn);
    }
    if (policy.require_toeplitz && facts.rss_hash != NicRssHash::Toeplitz) {
        detail::raise(report, NicAuditIssue::RssHashNotToeplitz,
            NicAuditSeverity::Warn);
    }
    if (policy.require_four_tuple_hash && !facts.rss_four_tuple_hash) {
        detail::raise(report, NicAuditIssue::RssFourTupleMissing,
            NicAuditSeverity::Warn);
    }

    if (facts.irq_distinct_local_cores.value()
        < policy.min_irq_local_cores.value()) {
        detail::raise(report, NicAuditIssue::IrqSpreadTooNarrow,
            NicAuditSeverity::Warn);
    }
    if (policy.require_numa_local_irqs && !facts.irq_handlers_numa_local) {
        detail::raise(report, NicAuditIssue::IrqRemoteNuma, NicAuditSeverity::Warn);
    }

    if (facts.rmem_max_bytes.value() < policy.expected_bdp_bytes.value()) {
        detail::raise(report, NicAuditIssue::RmemMaxBelowBdp,
            NicAuditSeverity::Warn);
    }
    if (facts.wmem_max_bytes.value() < policy.expected_bdp_bytes.value()) {
        detail::raise(report, NicAuditIssue::WmemMaxBelowBdp,
            NicAuditSeverity::Warn);
    }
    if (facts.netdev_budget_packets.value()
        < policy.min_netdev_budget_packets.value()) {
        detail::raise(report, NicAuditIssue::NetdevBudgetTooSmall,
            NicAuditSeverity::Warn);
    }
    if (policy.require_fq_qdisc && facts.tx_qdisc != NicTxQdisc::Fq) {
        detail::raise(report, NicAuditIssue::TxQdiscNotFq, NicAuditSeverity::Warn);
    }
    if (policy.low_latency_profile
        && facts.busy_poll_us < policy.min_busy_poll_us) {
        detail::raise(report, NicAuditIssue::BusyPollMissing,
            NicAuditSeverity::Warn);
    }

    return report;
}

}  // namespace crucible::cog
