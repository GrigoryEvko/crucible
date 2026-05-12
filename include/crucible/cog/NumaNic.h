#pragma once

// GAPS-194.  Read-only NIC <-> NUMA affinity substrate.
//
// This header verifies already-harvested affinity facts and provides a
// small rt-backed NUMA-node query helper.  It does not write
// /proc/irq, RPS, XPS, or irqbalance state; those mutation paths belong
// to the later NicConfig operator-policy task.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/rt/Topology.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/Refined.h>

#include <cstdint>
#include <string_view>

namespace crucible::cog {

using PositiveAffinityCount = safety::Positive<std::uint16_t>;

class [[nodiscard]] NumaNodeId {
    std::uint16_t value_ = UINT16_MAX;

public:
    constexpr NumaNodeId() noexcept = default;
    explicit constexpr NumaNodeId(std::uint16_t value) noexcept : value_{value} {}

    [[nodiscard]] static constexpr NumaNodeId unknown() noexcept {
        return NumaNodeId{};
    }

    [[nodiscard]] constexpr std::uint16_t raw() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_unknown() const noexcept {
        return value_ == UINT16_MAX;
    }

    constexpr auto operator<=>(NumaNodeId const&) const noexcept = default;
};

static_assert(sizeof(NumaNodeId) == sizeof(std::uint16_t));

[[nodiscard]] inline NumaNodeId
query_numa_for_nic(const char* sysfs_numa_node_path) noexcept {
    int const node = rt::numa_node_of_device(sysfs_numa_node_path);
    if (node < 0 || node > UINT16_MAX - 1) {
        return NumaNodeId::unknown();
    }
    return NumaNodeId{static_cast<std::uint16_t>(node)};
}

enum class NumaNicIssue : std::uint32_t {
    WrongCogKind             = 1u <<  0,
    NicNumaUnknown           = 1u <<  1,
    TargetNumaUnknown        = 1u <<  2,
    NicRemoteFromTarget      = 1u <<  3,
    IrqAffinityUnknown       = 1u <<  4,
    IrqSpreadTooNarrow       = 1u <<  5,
    IrqRemoteFromTarget      = 1u <<  6,
    RpsAffinityUnknown       = 1u <<  7,
    RpsRemoteFromTarget      = 1u <<  8,
    XpsAffinityUnknown       = 1u <<  9,
    XpsRemoteFromTarget      = 1u << 10,
    GpuDirectPeerRemote      = 1u << 11,
};

[[nodiscard]] constexpr std::string_view
numa_nic_issue_name(NumaNicIssue issue) noexcept {
    switch (issue) {
        case NumaNicIssue::WrongCogKind:        return "WrongCogKind";
        case NumaNicIssue::NicNumaUnknown:      return "NicNumaUnknown";
        case NumaNicIssue::TargetNumaUnknown:   return "TargetNumaUnknown";
        case NumaNicIssue::NicRemoteFromTarget: return "NicRemoteFromTarget";
        case NumaNicIssue::IrqAffinityUnknown:  return "IrqAffinityUnknown";
        case NumaNicIssue::IrqSpreadTooNarrow:  return "IrqSpreadTooNarrow";
        case NumaNicIssue::IrqRemoteFromTarget: return "IrqRemoteFromTarget";
        case NumaNicIssue::RpsAffinityUnknown:  return "RpsAffinityUnknown";
        case NumaNicIssue::RpsRemoteFromTarget: return "RpsRemoteFromTarget";
        case NumaNicIssue::XpsAffinityUnknown:  return "XpsAffinityUnknown";
        case NumaNicIssue::XpsRemoteFromTarget: return "XpsRemoteFromTarget";
        case NumaNicIssue::GpuDirectPeerRemote: return "GpuDirectPeerRemote";
        default:                                return "<unknown NumaNicIssue>";
    }
}

enum class NumaNicSeverity : std::uint8_t {
    Pass  = 0,
    Warn  = 1,
    Error = 2,
};

struct NumaNic_Misaligned : safety::diag::tag_base {
    static constexpr std::string_view name = "NumaNic_Misaligned";
    static constexpr std::string_view description =
        "NIC queue, IRQ, RPS, XPS, or peer placement is not NUMA-local "
        "to the target runtime placement.";
    static constexpr std::string_view remediation =
        "Use the operator-policy NicConfig path to steer IRQ, RPS, and "
        "XPS affinity to target-node-local cores; keep this verifier "
        "read-only.";
};

struct NumaNicFacts {
    NumaNodeId nic_node = NumaNodeId::unknown();
    NumaNodeId target_node = NumaNodeId::unknown();
    PositiveAffinityCount irq_handlers{std::uint16_t{1}};
    PositiveAffinityCount irq_handlers_on_target_node{std::uint16_t{1}};
    PositiveAffinityCount rx_queues{std::uint16_t{1}};
    PositiveAffinityCount rx_queues_on_target_node{std::uint16_t{1}};
    PositiveAffinityCount tx_queues{std::uint16_t{1}};
    PositiveAffinityCount tx_queues_on_target_node{std::uint16_t{1}};
    PositiveAffinityCount gpu_direct_peers{std::uint16_t{1}};
    PositiveAffinityCount gpu_direct_peers_on_target_node{std::uint16_t{1}};
    bool irq_affinity_known = false;
    bool rps_affinity_known = false;
    bool xps_affinity_known = false;
    bool gpu_direct_peer_numa_known = false;
};

struct NumaNicPolicy {
    NumaNodeId target_node = NumaNodeId::unknown();
    PositiveAffinityCount min_local_irq_handlers{std::uint16_t{1}};
    bool require_nic_on_target_node = true;
    bool require_irq_affinity_known = true;
    bool require_rps_affinity_known = true;
    bool require_xps_affinity_known = true;
    bool require_all_irqs_local = true;
    bool require_all_rps_local = true;
    bool require_all_xps_local = true;
    bool require_gpu_direct_peers_local = true;
    bool strict_unknown_topology = true;
};

struct NumaNicReport {
    safety::Bits<NumaNicIssue> issues{};
    NumaNicSeverity severity = NumaNicSeverity::Pass;
    NumaNodeId effective_target_node = NumaNodeId::unknown();

    [[nodiscard]] constexpr bool passes() const noexcept {
        return severity == NumaNicSeverity::Pass && issues.none();
    }

    [[nodiscard]] constexpr bool has(NumaNicIssue issue) const noexcept {
        return issues.test(issue);
    }
};

template <CogKind K>
concept NumaNicAuditableCog = (K == CogKind::NicPort) && HasCaps<K>;

namespace detail {

constexpr void
raise(NumaNicReport& report,
      NumaNicIssue issue,
      NumaNicSeverity severity) noexcept {
    report.issues.set(issue);
    if (static_cast<std::uint8_t>(severity)
        > static_cast<std::uint8_t>(report.severity)) {
        report.severity = severity;
    }
}

[[nodiscard]] constexpr NumaNicSeverity
unknown_severity(NumaNicPolicy const& policy) noexcept {
    return policy.strict_unknown_topology ? NumaNicSeverity::Error
                                          : NumaNicSeverity::Warn;
}

}  // namespace detail

template <CogKind K>
    requires NumaNicAuditableCog<K>
[[nodiscard]] constexpr NumaNicReport
verify_numa_pinning(CogIdentity const& identity,
                    caps_for_t<K> const& caps,
                    NumaNicFacts const& facts,
                    NumaNicPolicy const& policy = {}) noexcept {
    (void)caps;
    NumaNicReport report{};

    if (identity.kind != CogKind::NicPort) {
        detail::raise(report, NumaNicIssue::WrongCogKind, NumaNicSeverity::Error);
    }

    report.effective_target_node =
        policy.target_node.is_unknown() ? facts.target_node : policy.target_node;

    if (facts.nic_node.is_unknown()) {
        detail::raise(report, NumaNicIssue::NicNumaUnknown,
            detail::unknown_severity(policy));
    }
    if (report.effective_target_node.is_unknown()) {
        detail::raise(report, NumaNicIssue::TargetNumaUnknown,
            detail::unknown_severity(policy));
    }

    if (policy.require_nic_on_target_node
        && !facts.nic_node.is_unknown()
        && !report.effective_target_node.is_unknown()
        && facts.nic_node != report.effective_target_node) {
        detail::raise(report, NumaNicIssue::NicRemoteFromTarget,
            NumaNicSeverity::Error);
    }

    if (policy.require_irq_affinity_known && !facts.irq_affinity_known) {
        detail::raise(report, NumaNicIssue::IrqAffinityUnknown,
            detail::unknown_severity(policy));
    }
    if (facts.irq_handlers_on_target_node.value()
        < policy.min_local_irq_handlers.value()) {
        detail::raise(report, NumaNicIssue::IrqSpreadTooNarrow,
            NumaNicSeverity::Warn);
    }
    if (policy.require_all_irqs_local
        && facts.irq_handlers_on_target_node.value()
            < facts.irq_handlers.value()) {
        detail::raise(report, NumaNicIssue::IrqRemoteFromTarget,
            NumaNicSeverity::Warn);
    }

    if (policy.require_rps_affinity_known && !facts.rps_affinity_known) {
        detail::raise(report, NumaNicIssue::RpsAffinityUnknown,
            detail::unknown_severity(policy));
    }
    if (policy.require_all_rps_local
        && facts.rx_queues_on_target_node.value() < facts.rx_queues.value()) {
        detail::raise(report, NumaNicIssue::RpsRemoteFromTarget,
            NumaNicSeverity::Warn);
    }

    if (policy.require_xps_affinity_known && !facts.xps_affinity_known) {
        detail::raise(report, NumaNicIssue::XpsAffinityUnknown,
            detail::unknown_severity(policy));
    }
    if (policy.require_all_xps_local
        && facts.tx_queues_on_target_node.value() < facts.tx_queues.value()) {
        detail::raise(report, NumaNicIssue::XpsRemoteFromTarget,
            NumaNicSeverity::Warn);
    }

    if (policy.require_gpu_direct_peers_local
        && facts.gpu_direct_peer_numa_known
        && facts.gpu_direct_peers_on_target_node.value()
            < facts.gpu_direct_peers.value()) {
        detail::raise(report, NumaNicIssue::GpuDirectPeerRemote,
            NumaNicSeverity::Warn);
    }

    return report;
}

}  // namespace crucible::cog
