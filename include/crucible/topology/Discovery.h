#pragma once

// GAPS-111 substrate: deterministic topology discovery facts and parsers.
//
// Live OS harvesters are intentionally separated from this bounded substrate.
// TopologyGraph is a non-owning span carrier; returning it from a function
// backed by local vectors would dangle. Discovery therefore fills an owning
// DiscoverySnapshot<N,E>, then mints a TopologyGraph view over that storage.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Tagged.h>
#include <crucible/topology/TopologyGraph.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::topology {

using ExternalDiscoveryText =
    safety::Tagged<std::string_view, safety::source::External>;
using VendorDiscoveryString =
    safety::Tagged<std::string_view, safety::source::Vendor>;

enum class DiscoveryError : std::uint8_t {
    EmptyInput = 0,
    MalformedRecord = 1,
    TooManyNodes = 2,
    TooManyEdges = 3,
    InvalidNodeIndex = 4,
    InvalidInterfaceName = 5,
    MissingRequiredField = 6,
    UnsupportedSource = 7,
    RediscoveryRequiresBg = 8,
};

enum class DiscoverySource : std::uint8_t {
    PcieLspci = 0,
    PcieSysfs = 1,
    NicSysfs = 2,
    EthtoolInfo = 3,
    EthtoolFeatures = 4,
    Lldp = 5,
    Gpu = 6,
    Nvme = 7,
    NvSwitch = 8,
    Optical = 9,
    Udev = 10,
};

enum class DiscoveryOutcome : std::uint8_t {
    NotAttempted = 0,
    Complete = 1,
    Partial = 2,
    Failed = 3,
};

enum class DiscoveryNodeKind : std::uint8_t {
    Unknown = 0,
    Gpu = 1,
    NicPort = 2,
    NicCard = 3,
    NvSwitch = 4,
    NvmeNamespace = 5,
    NvmeDrive = 6,
    PcieRoot = 7,
    PcieLaneGroup = 8,
    OpticalTransceiver = 9,
};

struct DiscoverySourceStatus {
    DiscoverySource source = DiscoverySource::PcieLspci;
    DiscoveryOutcome outcome = DiscoveryOutcome::NotAttempted;
    std::uint16_t records_seen = 0;
    std::uint16_t records_admitted = 0;
    DiscoveryError error = DiscoveryError::EmptyInput;
};

struct DiscoveryReport {
    std::array<DiscoverySourceStatus, 16> statuses{};
    std::uint8_t size = 0;

    [[nodiscard]] constexpr std::span<const DiscoverySourceStatus>
    view() const noexcept {
        return {statuses.data(), size};
    }

    [[nodiscard]] constexpr bool push(DiscoverySourceStatus status) noexcept {
        if (size == statuses.size()) {
            return false;
        }
        statuses[size++] = status;
        return true;
    }
};

struct DiscoveryNodeFact {
    cog::Uuid uuid{};
    cog::CogLevel level = cog::CogLevel::L0_Atomic;
    cog::CogKind kind = cog::CogKind::Gpu;
    VendorDiscoveryString vendor{std::string_view{}};
    VendorDiscoveryString model{std::string_view{}};
    VendorDiscoveryString driver{std::string_view{}};
    VendorDiscoveryString firmware{std::string_view{}};
    VendorDiscoveryString bus_info{std::string_view{}};
    std::int16_t numa_node = -1;
    cog::NicPortTargetCaps nic_caps{};
};

struct DiscoveryEdgeFact {
    std::uint16_t from_node = 0;
    std::uint16_t to_node = 0;
    LinkKind kind = LinkKind::Unknown;
    std::uint64_t bandwidth_bytes_per_sec = 0;
    std::uint64_t rtt_ns_p50 = 0;
    std::uint64_t rtt_ns_p99 = 0;
    float drop_rate = 0.0f;
};

template <std::size_t MaxNodes, std::size_t MaxEdges>
concept DiscoveryShape = MaxNodes > 0 && MaxEdges > 0;

template <class Ctx>
concept CtxFitsDiscoveryInit =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Init>;

template <class Ctx>
concept CtxFitsDiscoveryBg =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Bg>;

[[nodiscard]] std::string_view discovery_error_name(DiscoveryError error) noexcept;
[[nodiscard]] std::string_view discovery_source_name(DiscoverySource source) noexcept;
[[nodiscard]] std::string_view discovery_outcome_name(DiscoveryOutcome outcome) noexcept;
[[nodiscard]] std::string_view discovery_node_kind_name(DiscoveryNodeKind kind) noexcept;

[[nodiscard]] constexpr ExternalDiscoveryText
tag_external_discovery_text(std::string_view text) noexcept {
    return ExternalDiscoveryText{text};
}

[[nodiscard]] constexpr VendorDiscoveryString
tag_vendor_discovery_string(std::string_view text) noexcept {
    return VendorDiscoveryString{text};
}

[[nodiscard]] constexpr cog::CogKind
cog_kind_from_discovery(DiscoveryNodeKind kind) noexcept {
    switch (kind) {
        case DiscoveryNodeKind::Gpu:                return cog::CogKind::Gpu;
        case DiscoveryNodeKind::NicPort:            return cog::CogKind::NicPort;
        case DiscoveryNodeKind::NicCard:            return cog::CogKind::NicCard;
        case DiscoveryNodeKind::NvSwitch:           return cog::CogKind::NvSwitch;
        case DiscoveryNodeKind::NvmeNamespace:      return cog::CogKind::NvmeNamespace;
        case DiscoveryNodeKind::NvmeDrive:          return cog::CogKind::NvmeDrive;
        case DiscoveryNodeKind::PcieRoot:           return cog::CogKind::PcieRoot;
        case DiscoveryNodeKind::PcieLaneGroup:      return cog::CogKind::PcieLaneGroup;
        case DiscoveryNodeKind::OpticalTransceiver: return cog::CogKind::OpticalTransceiver;
        case DiscoveryNodeKind::Unknown:            return cog::CogKind::PcieLaneGroup;
        default:                                    return cog::CogKind::PcieLaneGroup;
    }
}

[[nodiscard]] constexpr cog::CogLevel
default_level_for(cog::CogKind kind) noexcept {
    switch (kind) {
        case cog::CogKind::NicCard:
        case cog::CogKind::NvmeDrive:
        case cog::CogKind::PcieRoot:
            return cog::CogLevel::L1_Component;
        default:
            return cog::CogLevel::L0_Atomic;
    }
}

[[nodiscard]] constexpr std::uint64_t
stable_discovery_hash(std::string_view a,
                      std::string_view b = {},
                      std::string_view c = {}) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](std::string_view s) constexpr noexcept {
        for (char ch : s) {
            h ^= static_cast<std::uint64_t>(
                static_cast<unsigned char>(ch));
            h *= 1099511628211ull;
        }
        h ^= 0xffu;
        h *= 1099511628211ull;
    };
    mix(a);
    mix(b);
    mix(c);
    return h == 0 ? 1 : h;
}

template <std::size_t MaxNodes, std::size_t MaxEdges>
    requires DiscoveryShape<MaxNodes, MaxEdges>
class DiscoverySnapshot {
public:
    [[nodiscard]] constexpr std::span<const cog::CogIdentity>
    nodes() const noexcept {
        return {nodes_.data(), node_count_};
    }

    [[nodiscard]] constexpr std::span<const TopologyEdge>
    edges() const noexcept {
        return {edges_.data(), edge_count_};
    }

    [[nodiscard]] constexpr std::span<const DiscoveryNodeFact>
    node_facts() const noexcept {
        return {node_facts_.data(), node_count_};
    }

    [[nodiscard]] constexpr std::span<const DiscoveryEdgeFact>
    edge_facts() const noexcept {
        return {edge_facts_.data(), edge_count_};
    }

    [[nodiscard]] constexpr std::size_t node_count() const noexcept {
        return node_count_;
    }

    [[nodiscard]] constexpr std::size_t edge_count() const noexcept {
        return edge_count_;
    }

    [[nodiscard]] constexpr DiscoveryReport const& report() const noexcept {
        return report_;
    }

    [[nodiscard]] constexpr std::expected<std::uint16_t, DiscoveryError>
    add_node(DiscoveryNodeFact fact) noexcept {
        if (node_count_ == MaxNodes) {
            return std::unexpected(DiscoveryError::TooManyNodes);
        }
        const std::uint16_t idx = static_cast<std::uint16_t>(node_count_);
        if (fact.uuid.is_zero()) {
            const std::uint64_t lo = stable_discovery_hash(
                fact.bus_info.value(), fact.vendor.value(), fact.model.value());
            fact.uuid = cog::Uuid{0xD15C0'111ull, lo};
        }
        fact.level = fact.level == cog::CogLevel::L0_Atomic
            ? default_level_for(fact.kind)
            : fact.level;
        node_facts_[node_count_] = fact;
        nodes_[node_count_] = cog::CogIdentity{
            .uuid = fact.uuid,
            .level = fact.level,
            .kind = fact.kind,
            .vendor = fact.vendor,
            .model = fact.model,
            .firmware_revision = safety::Tagged<std::uint64_t,
                safety::source::Vendor>{stable_discovery_hash(fact.firmware.value())},
            .bios_revision = safety::Tagged<std::uint64_t,
                safety::source::Vendor>{0},
        };
        ++node_count_;
        return idx;
    }

    [[nodiscard]] constexpr std::expected<void, DiscoveryError>
    update_node(std::uint16_t idx, DiscoveryNodeFact fact) noexcept {
        if (idx >= node_count_) {
            return std::unexpected(DiscoveryError::InvalidNodeIndex);
        }
        node_facts_[idx] = fact;
        nodes_[idx].uuid = fact.uuid;
        nodes_[idx].level = fact.level;
        nodes_[idx].kind = fact.kind;
        nodes_[idx].vendor = fact.vendor;
        nodes_[idx].model = fact.model;
        nodes_[idx].firmware_revision = safety::Tagged<std::uint64_t,
            safety::source::Vendor>{stable_discovery_hash(fact.firmware.value())};
        return {};
    }

    [[nodiscard]] constexpr std::expected<std::uint16_t, DiscoveryError>
    add_edge(DiscoveryEdgeFact fact) noexcept {
        if (edge_count_ == MaxEdges) {
            return std::unexpected(DiscoveryError::TooManyEdges);
        }
        if (fact.from_node >= node_count_ || fact.to_node >= node_count_) {
            return std::unexpected(DiscoveryError::InvalidNodeIndex);
        }
        const std::uint16_t idx = static_cast<std::uint16_t>(edge_count_);
        edge_facts_[edge_count_] = fact;
        edges_[edge_count_] = TopologyEdge{
            .id = EdgeId{idx},
            .kind = fact.kind,
            .peer = &nodes_[fact.to_node],
            .bandwidth_bytes_per_sec = safety::Tagged<std::uint64_t,
                safety::source::Calibrated>{fact.bandwidth_bytes_per_sec},
            .rtt_ns_p50 = safety::Tagged<std::uint64_t,
                safety::source::Calibrated>{fact.rtt_ns_p50},
            .rtt_ns_p99 = safety::Tagged<std::uint64_t,
                safety::source::Calibrated>{fact.rtt_ns_p99},
            .drop_rate = safety::Tagged<float,
                safety::source::Calibrated>{fact.drop_rate},
        };
        ++edge_count_;
        return idx;
    }

    [[nodiscard]] constexpr bool record(DiscoverySourceStatus status) noexcept {
        return report_.push(status);
    }

    template <class Ctx>
        requires CtxFitsDiscoveryInit<Ctx>
    [[nodiscard]] constexpr TopologyGraph graph(Ctx const& ctx) const noexcept {
        return mint_topology_graph(ctx, nodes(), edges());
    }

private:
    std::array<DiscoveryNodeFact, MaxNodes> node_facts_{};
    std::array<DiscoveryEdgeFact, MaxEdges> edge_facts_{};
    std::array<cog::CogIdentity, MaxNodes> nodes_{};
    std::array<TopologyEdge, MaxEdges> edges_{};
    std::size_t node_count_ = 0;
    std::size_t edge_count_ = 0;
    DiscoveryReport report_{};
};

template <std::size_t MaxNodes, std::size_t MaxEdges, class Ctx>
    requires DiscoveryShape<MaxNodes, MaxEdges> && CtxFitsDiscoveryInit<Ctx>
[[nodiscard]] constexpr DiscoverySnapshot<MaxNodes, MaxEdges>
mint_discovery_snapshot(Ctx const&) noexcept {
    return {};
}

template <std::size_t MaxNodes, std::size_t MaxEdges, class Ctx>
    requires DiscoveryShape<MaxNodes, MaxEdges> && CtxFitsDiscoveryInit<Ctx>
[[nodiscard]] constexpr TopologyGraph
discover_local_topology(Ctx const& ctx,
                        DiscoverySnapshot<MaxNodes, MaxEdges>& snapshot) noexcept {
    static_cast<void>(snapshot.record(DiscoverySourceStatus{
        .source = DiscoverySource::PcieLspci,
        .outcome = DiscoveryOutcome::NotAttempted,
        .error = DiscoveryError::UnsupportedSource,
    }));
    return snapshot.graph(ctx);
}

template <class Ctx>
    requires CtxFitsDiscoveryBg<Ctx>
[[nodiscard]] constexpr std::expected<DiscoverySourceStatus, DiscoveryError>
notify_rediscovery_trigger(Ctx const&, DiscoverySource source) noexcept {
    return DiscoverySourceStatus{
        .source = source,
        .outcome = DiscoveryOutcome::NotAttempted,
        .error = DiscoveryError::UnsupportedSource,
    };
}

using DefaultDiscoverySnapshot = DiscoverySnapshot<64, 128>;

[[nodiscard]] std::expected<DiscoverySourceStatus, DiscoveryError>
parse_lspci_vmm_tree(ExternalDiscoveryText text,
                     DefaultDiscoverySnapshot& snapshot) noexcept;

[[nodiscard]] std::expected<DiscoverySourceStatus, DiscoveryError>
parse_ethtool_info(ExternalDiscoveryText text,
                   DefaultDiscoverySnapshot& snapshot,
                   std::uint16_t node_index) noexcept;

[[nodiscard]] std::expected<safety::Bits<cog::NicFeature>, DiscoveryError>
parse_ethtool_features(ExternalDiscoveryText text) noexcept;

[[nodiscard]] std::expected<DiscoverySourceStatus, DiscoveryError>
parse_lldp_neighbors(ExternalDiscoveryText text,
                     DefaultDiscoverySnapshot& snapshot) noexcept;

static_assert(sizeof(ExternalDiscoveryText) == sizeof(std::string_view));
static_assert(sizeof(VendorDiscoveryString) == sizeof(std::string_view));
static_assert(DiscoveryShape<1, 1>);
static_assert(!DiscoveryShape<0, 1>);
static_assert(std::is_trivially_destructible_v<DiscoveryNodeFact>);
static_assert(std::is_trivially_destructible_v<DiscoveryEdgeFact>);

}  // namespace crucible::topology
