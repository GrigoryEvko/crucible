#pragma once

// A read-only check that a NIC, its interrupts and its queues sit on
// the same NUMA node as the work they serve. The facts are gathered
// elsewhere and handed in. Nothing here steers an interrupt or writes
// a packet-steering map.
//
// foundation::reflect::enum_name gives the name of each enumerator.

#include <crucible/cog/AuditFindings.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/warden/CpuTopology.h>
#include <fixy/Refined.h>
#include <foundation/diag/Catalog.h>

#include <cstdint>
#include <string_view>

namespace crucible::cog {

using PositiveAffinityCount = ::fixy::Positive<std::uint16_t>;

// The floor of every count. A fact or a policy field starts here until
// the caller supplies a measured or chosen value.
inline constexpr PositiveAffinityCount single_affinity = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{1});

class [[nodiscard]] NumaNodeId {
    std::uint16_t value_ = UINT16_MAX;

public:
    constexpr NumaNodeId() noexcept = default;
    explicit constexpr NumaNodeId(std::uint16_t value) noexcept : value_{value} {}

    [[nodiscard]] static constexpr NumaNodeId unknown() noexcept { return NumaNodeId{}; }

    [[nodiscard]] constexpr std::uint16_t raw() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_unknown() const noexcept { return value_ == UINT16_MAX; }

    constexpr auto operator<=>(NumaNodeId const&) const noexcept = default;
};

// The query reads a sysfs file, so it takes a context that owns IO and
// Block.
template <::fixy::CtxFitsFileOpen Ctx>
[[nodiscard]] inline NumaNodeId query_numa_for_nic(Ctx const& ctx, const char* sysfs_numa_node_path) noexcept {
    int const node = warden::numa_node_of_device(ctx, sysfs_numa_node_path);
    if (node < 0 || node > UINT16_MAX - 1) {
        return NumaNodeId::unknown();
    }
    return NumaNodeId{static_cast<std::uint16_t>(node)};
}

enum class NumaNicIssue : std::uint32_t {
    WrongCogKind = 1u << 0,
    NicNumaUnknown = 1u << 1,
    TargetNumaUnknown = 1u << 2,
    NicRemoteFromTarget = 1u << 3,
    IrqAffinityUnknown = 1u << 4,
    IrqSpreadTooNarrow = 1u << 5,
    IrqRemoteFromTarget = 1u << 6,
    RpsAffinityUnknown = 1u << 7,
    RpsRemoteFromTarget = 1u << 8,
    XpsAffinityUnknown = 1u << 9,
    XpsRemoteFromTarget = 1u << 10,
    GpuDirectPeerRemote = 1u << 11,
};

struct NumaNic_Misaligned : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "NumaNic_Misaligned";
    static constexpr std::string_view description = "NIC queue, IRQ, RPS, XPS, or peer placement is not NUMA-local "
                                                    "to the target runtime placement.";
    static constexpr std::string_view remediation = "Use the operator-policy NicConfig path to steer IRQ, RPS, and "
                                                    "XPS affinity to target-node-local cores; keep this verifier "
                                                    "read-only.";
};

struct NumaNicFacts {
    NumaNodeId nic_node = NumaNodeId::unknown();
    NumaNodeId target_node = NumaNodeId::unknown();
    PositiveAffinityCount irq_handlers = single_affinity;
    PositiveAffinityCount irq_handlers_on_target_node = single_affinity;
    PositiveAffinityCount rx_queues = single_affinity;
    PositiveAffinityCount rx_queues_on_target_node = single_affinity;
    PositiveAffinityCount tx_queues = single_affinity;
    PositiveAffinityCount tx_queues_on_target_node = single_affinity;
    PositiveAffinityCount gpu_direct_peers = single_affinity;
    PositiveAffinityCount gpu_direct_peers_on_target_node = single_affinity;
    bool irq_affinity_known = false;
    bool rps_affinity_known = false;
    bool xps_affinity_known = false;
    bool gpu_direct_peer_numa_known = false;
};

struct NumaNicPolicy {
    NumaNodeId target_node = NumaNodeId::unknown();
    PositiveAffinityCount min_local_irq_handlers = single_affinity;
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

struct NumaNicReport : AuditFindings<NumaNicIssue> {
    NumaNodeId effective_target_node = NumaNodeId::unknown();
};

template <CogKind K>
concept NumaNicAuditableCog = (K == CogKind::NicPort) && HasCaps<K>;

namespace detail {

[[nodiscard]] constexpr AuditSeverity unknown_severity(NumaNicPolicy const& policy) noexcept {
    return policy.strict_unknown_topology ? AuditSeverity::Error : AuditSeverity::Warn;
}

}  // namespace detail

template <CogKind K>
    requires NumaNicAuditableCog<K>
[[nodiscard]] constexpr NumaNicReport verify_numa_pinning(CogIdentity const& identity, caps_for_t<K> const& caps,
                                                          NumaNicFacts const& facts,
                                                          NumaNicPolicy const& policy = {}) noexcept {
    (void)caps;
    NumaNicReport report{};

    if (identity.kind != CogKind::NicPort) {
        report.raise(NumaNicIssue::WrongCogKind, AuditSeverity::Error);
    }

    report.effective_target_node = policy.target_node.is_unknown() ? facts.target_node : policy.target_node;

    if (facts.nic_node.is_unknown()) {
        report.raise(NumaNicIssue::NicNumaUnknown, detail::unknown_severity(policy));
    }
    if (report.effective_target_node.is_unknown()) {
        report.raise(NumaNicIssue::TargetNumaUnknown, detail::unknown_severity(policy));
    }

    if (policy.require_nic_on_target_node && !facts.nic_node.is_unknown() && !report.effective_target_node.is_unknown()
        && facts.nic_node != report.effective_target_node) {
        report.raise(NumaNicIssue::NicRemoteFromTarget, AuditSeverity::Error);
    }

    if (policy.require_irq_affinity_known && !facts.irq_affinity_known) {
        report.raise(NumaNicIssue::IrqAffinityUnknown, detail::unknown_severity(policy));
    }
    if (facts.irq_handlers_on_target_node.value() < policy.min_local_irq_handlers.value()) {
        report.raise(NumaNicIssue::IrqSpreadTooNarrow, AuditSeverity::Warn);
    }
    if (policy.require_all_irqs_local && facts.irq_handlers_on_target_node.value() < facts.irq_handlers.value()) {
        report.raise(NumaNicIssue::IrqRemoteFromTarget, AuditSeverity::Warn);
    }

    if (policy.require_rps_affinity_known && !facts.rps_affinity_known) {
        report.raise(NumaNicIssue::RpsAffinityUnknown, detail::unknown_severity(policy));
    }
    if (policy.require_all_rps_local && facts.rx_queues_on_target_node.value() < facts.rx_queues.value()) {
        report.raise(NumaNicIssue::RpsRemoteFromTarget, AuditSeverity::Warn);
    }

    if (policy.require_xps_affinity_known && !facts.xps_affinity_known) {
        report.raise(NumaNicIssue::XpsAffinityUnknown, detail::unknown_severity(policy));
    }
    if (policy.require_all_xps_local && facts.tx_queues_on_target_node.value() < facts.tx_queues.value()) {
        report.raise(NumaNicIssue::XpsRemoteFromTarget, AuditSeverity::Warn);
    }

    if (policy.require_gpu_direct_peers_local && facts.gpu_direct_peer_numa_known
        && facts.gpu_direct_peers_on_target_node.value() < facts.gpu_direct_peers.value()) {
        report.raise(NumaNicIssue::GpuDirectPeerRemote, AuditSeverity::Warn);
    }

    return report;
}

}  // namespace crucible::cog
